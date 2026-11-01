#pragma once

// Port of the reference src/worklet/processor.ts (SynthProcessor).
// Not thread-safe: call everything from the audio thread. Allocation happens
// only in the constructor.

#include "sg/effects.h"
#include "sg/voice.h"

#include <memory>
#include <optional>

namespace sg {

class Processor {
public:
    static constexpr int kScopeSize = 1024;

    explicit Processor(double sampleRate, std::uint32_t seed = 0);

    // --- reference ToWorklet messages
    void setParam(int index, double value) noexcept;
    void noteOn(int note, double velocity) noexcept;
    void noteOff(int note) noexcept;
    void sustain(bool down) noexcept;
    void pitchBend(double v) noexcept { pitchBend_ = v; }
    void modWheel(double v) noexcept { modWheel_ = v; }
    void aftertouch(double v) noexcept { aftertouch_ = v; }
    void setModSlot(int slot, const std::optional<ModSlotState>& state) noexcept;
    void setLfoShape(int lfo, const LfoShape& shape) noexcept;
    void setWavetable(int osc, const WavetableData* table) noexcept { if (osc >= 0 && osc < 3) tables_[osc] = table; }
    void setSample(const Sample* sample) noexcept { sample_ = sample; }
    void setFxOrder(const int* order, int count) noexcept;
    void allNotesOff() noexcept;

    /** Render one kBlockSize block (the AudioWorklet render quantum). */
    void process(float* l, float* r) noexcept;

    double sampleRate() const noexcept { return sr_; }
    float param(int index) const noexcept { return base_[index]; }
    Rng& rng() noexcept { return rng_; }

    // --- telemetry (reference 'scope' / 'status'), refreshed every 1024 samples
    struct Telemetry {
        float scopeL[kScopeSize];
        float scopeR[kScopeSize];
        int voices;
        double peakL, peakR;
        float sources[kNumModSources];
    };
    /** True once per 1024 samples; the caller copies `telemetry()` and calls clear. */
    bool telemetryReady() const noexcept { return telemetryReady_; }
    void clearTelemetryReady() noexcept { telemetryReady_ = false; }
    const Telemetry& telemetry() const noexcept { return telemetry_; }
    int activeVoices() const noexcept;

private:
    void rebuildRoutes() noexcept;
    void updateGlobalLfos(double bpm) noexcept;
    void updateGlobalMods() noexcept;
    double gv(int index) const noexcept;
    void runFx(float* l, float* r, int n, double bpm) noexcept;

    double sr_;
    Rng rng_;
    float base_[kNumParams];
    std::vector<std::unique_ptr<Voice>> voices_;
    double noteAge_ = 0;
    bool sustainDown_ = false;

    std::optional<ModSlotState> slots_[kMaxModSlots];
    ModRoutes routes_;

    const WavetableData* tables_[3] = {nullptr, nullptr, nullptr};
    LfoShape lfoShapes_[8];
    const Sample* sample_ = nullptr;

    float lfoGlobalPhases_[8] = {};
    float lfoBeatPhases_[8] = {};
    float lfoFreqs_[8] = {};
    double beatCounter_ = 0;

    double pitchBend_ = 0, modWheel_ = 0, aftertouch_ = 0;

    float globalOffsets_[kNumParams] = {};
    float globalSources_[kNumModSources] = {};

    int fxOrder_[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    Chorus chorus_;
    Phaser phaser_;
    Flanger flanger_;
    StereoDelay delay_;
    Reverb reverb_;
    Eq3 eq_;
    Compressor comp_;
    FxDistortion fxdist_;

    float scopeL_[kScopeSize] = {}, scopeR_[kScopeSize] = {};
    int scopePos_ = 0;
    double peakL_ = 0, peakR_ = 0;
    Telemetry telemetry_{};
    bool telemetryReady_ = false;

    VoiceContext ctx_;
};

} // namespace sg
