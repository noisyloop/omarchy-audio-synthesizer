// VST3 / LV2 / JACK-standalone wrapper around the same sg_core the daemon
// uses. Parameters are the 223 reference parameters, normalized 0..1; the 59
// factory presets are programs (so their mod matrix and LFO shapes load too).
// No custom GUI: hosts show their generic parameter UI.

#include "DistrhoPlugin.hpp"

#include "sg/patch.h"
#include "sg/processor.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <random>

START_NAMESPACE_DISTRHO

namespace {
const char* const kFactoryPresets[] = {
#include "factory_presets.inc"
};
constexpr uint32_t kNumPrograms = sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]);
}

class SoundgineerPlugin : public Plugin {
public:
    SoundgineerPlugin() : Plugin(sg::kNumParams, kNumPrograms, 1) {
        bank_.prebuild();
        for (int i = 0; i < 6; i++) tables_[i] = bank_.named(i);  // no bank lock on the audio thread
        for (int m = 0; m < 4; m++) {
            char id[32];
            std::snprintf(id, sizeof id, "macro%d.value", m + 1);
            macroIdx_[m] = sg::paramIndex(id);
        }
        for (int o = 0; o < 3; o++) {
            char id[32];
            std::snprintf(id, sizeof id, "osc%d.wavetable", o + 1);
            wtIdx_[o] = sg::paramIndex(id);
        }
        rebuild(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Soundgineer"; }
    const char* getDescription() const override { return "Wavetable synthesizer: 3 wavetable oscillators, 2 filters, 8 effects, 32-slot mod matrix."; }
    const char* getMaker() const override { return "Soundgineer contributors"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initParameter(uint32_t index, Parameter& p) override {
        const sg::ParamDef& d = sg::kParams[index];
        p.hints = kParameterIsAutomatable;
        p.name = d.name;
        String sym(d.id);
        sym.replace('.', '_');
        p.symbol = sym;
        p.groupId = kPortGroupNone;
        char longName[96];
        std::snprintf(longName, sizeof longName, "%s %s", d.group, d.name);
        p.name = longName;
        p.ranges.min = 0.0f;
        p.ranges.max = 1.0f;
        p.ranges.def = static_cast<float>(sg::valueToNorm(d, d.def));
    }

    void initProgramName(uint32_t index, String& name) override {
        try { name = sg::Patch::fromJson(sg::Json::parse(kFactoryPresets[index])).name.c_str(); }
        catch (...) { name = "Preset"; }
    }

    void loadProgram(uint32_t index) override {
        if (index >= kNumPrograms) return;
        try {
            patch_ = sg::Patch::fromJson(sg::Json::parse(kFactoryPresets[index]));
            sg::syncPatch(*proc_, patch_, bank_);
        } catch (...) {}
    }

    void initState(uint32_t, State& state) override {
        state.key = "patch";
        state.label = "Mod matrix, LFO shapes, FX order";
        state.defaultValue = "";
        state.hints = 0;
    }

    // JSON with the non-parameter parts of the patch (same shape as a preset).
    String getState(const char* key) const override {
        if (std::strcmp(key, "patch") != 0) return String();
        sg::Json full = patch_.toJson();
        sg::Json out = sg::Json::object();
        out.set("version", 1);
        out.set("name", patch_.name);
        out.set("mods", full["mods"]);
        out.set("lfoShapes", full["lfoShapes"]);
        out.set("fxOrder", full["fxOrder"]);
        return String(out.dump().c_str());
    }

    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, "patch") != 0 || value == nullptr || *value == '\0') return;
        try {
            const sg::Patch parsed = sg::Patch::fromJson(sg::Json::parse(value));
            patch_.name = parsed.name;
            for (int s = 0; s < sg::kMaxModSlots; s++) { patch_.slots[s] = parsed.slots[s]; proc_->setModSlot(s, parsed.slots[s]); }
            for (int l = 0; l < 8; l++) { patch_.shapes[l] = parsed.shapes[l]; proc_->setLfoShape(l, parsed.shapes[l]); }
            std::copy(parsed.fxOrder, parsed.fxOrder + 8, patch_.fxOrder);
            proc_->setFxOrder(parsed.fxOrder, 8);
        } catch (...) {}
    }

    float getParameterValue(uint32_t index) const override { return patch_.values[index]; }

    void setParameterValue(uint32_t index, float value) override {
        if (index >= static_cast<uint32_t>(sg::kNumParams)) return;
        patch_.values[index] = std::clamp(value, 0.0f, 1.0f);
        proc_->setParam(static_cast<int>(index), patch_.values[index]);
        for (int o = 0; o < 3; o++)
            if (static_cast<int>(index) == wtIdx_[o])
                proc_->setWavetable(o, tables_[std::min(5, static_cast<int>(std::floor(patch_.values[index] * 6 + 0.5)))]);
    }

    void sampleRateChanged(double sr) override { rebuild(sr); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override {
        float* outL = outputs[0];
        float* outR = outputs[1];
        uint32_t m = 0;
        for (uint32_t i = 0; i < frames; i++) {
            if (pos_ >= sg::kBlockSize) {
                while (m < midiCount && midi[m].frame <= i) { handleMidi(midi[m]); m++; }
                proc_->process(blockL_, blockR_);
                proc_->clearTelemetryReady();
                pos_ = 0;
            }
            outL[i] = blockL_[pos_];
            outR[i] = blockR_[pos_];
            pos_++;
        }
        for (; m < midiCount; m++) handleMidi(midi[m]);
    }

private:
    void rebuild(double sr) {
        std::random_device rd;
        proc_ = std::make_unique<sg::Processor>(sr, rd());
        sg::syncPatch(*proc_, patch_, bank_);
        pos_ = sg::kBlockSize;
    }

    void handleMidi(const MidiEvent& ev) {
        const uint8_t* d = ev.size > MidiEvent::kDataSize ? ev.dataExt : ev.data;
        if (ev.size < 2) return;
        const int status = d[0] & 0xF0, a = d[1] & 0x7F, b = ev.size > 2 ? (d[2] & 0x7F) : 0;
        switch (status) {
        case 0x90: if (b > 0) proc_->noteOn(a, b / 127.0); else proc_->noteOff(a); break;
        case 0x80: proc_->noteOff(a); break;
        case 0xB0:
            if (a == 1) proc_->modWheel(b / 127.0);
            else if (a == 64) proc_->sustain(b >= 64);
            else if (a >= 20 && a <= 23) { patch_.values[macroIdx_[a - 20]] = b / 127.0f; proc_->setParam(macroIdx_[a - 20], b / 127.0); }
            else if (a == 120 || a == 123) proc_->allNotesOff();
            break;
        case 0xE0: proc_->pitchBend(std::max(-1.0, (((b << 7) | a) - 8192) / 8192.0)); break;
        case 0xD0: proc_->aftertouch(a / 127.0); break;
        default: break;
        }
    }

    sg::Patch patch_;
    sg::WavetableBank bank_;
    std::unique_ptr<sg::Processor> proc_;
    float blockL_[sg::kBlockSize] = {}, blockR_[sg::kBlockSize] = {};
    int pos_ = sg::kBlockSize;
    int macroIdx_[4];
    int wtIdx_[3];
    const sg::WavetableData* tables_[6] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundgineerPlugin)
};

Plugin* createPlugin() { return new SoundgineerPlugin(); }

END_NAMESPACE_DISTRHO
