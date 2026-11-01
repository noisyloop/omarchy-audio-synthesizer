#include "sg/processor.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sg {

namespace {

struct FxIdx {
    int chOn, chRate, chDepth, chMix;
    int phOn, phRate, phDepth, phFb, phMix;
    int flOn, flRate, flDepth, flFb, flMix;
    int dlOn, dlSync, dlDiv, dlTime, dlFb, dlPp, dlMix;
    int rvOn, rvSize, rvDamp, rvWidth, rvMix;
    int eqOn, eqLow, eqMid, eqMidF, eqHigh;
    int cpOn, cpTh, cpRatio, cpAtk, cpRel, cpMk;
    int fdOn, fdDrive, fdTone, fdMix;
    int lfoRate[8], lfoSync[8], lfoDiv[8], lfoPhase[8];
    int macro[4];
    int volume, bpm, polyphony, bendRange;

    FxIdx() {
        auto i = [](const char* id) { return paramIndex(id); };
        chOn = i("chorus.enabled"); chRate = i("chorus.rate"); chDepth = i("chorus.depth"); chMix = i("chorus.mix");
        phOn = i("phaser.enabled"); phRate = i("phaser.rate"); phDepth = i("phaser.depth"); phFb = i("phaser.feedback"); phMix = i("phaser.mix");
        flOn = i("flanger.enabled"); flRate = i("flanger.rate"); flDepth = i("flanger.depth"); flFb = i("flanger.feedback"); flMix = i("flanger.mix");
        dlOn = i("delay.enabled"); dlSync = i("delay.sync"); dlDiv = i("delay.division"); dlTime = i("delay.time");
        dlFb = i("delay.feedback"); dlPp = i("delay.pingpong"); dlMix = i("delay.mix");
        rvOn = i("reverb.enabled"); rvSize = i("reverb.size"); rvDamp = i("reverb.damp"); rvWidth = i("reverb.width"); rvMix = i("reverb.mix");
        eqOn = i("eq.enabled"); eqLow = i("eq.low_gain"); eqMid = i("eq.mid_gain"); eqMidF = i("eq.mid_freq"); eqHigh = i("eq.high_gain");
        cpOn = i("comp.enabled"); cpTh = i("comp.threshold"); cpRatio = i("comp.ratio"); cpAtk = i("comp.attack");
        cpRel = i("comp.release"); cpMk = i("comp.makeup");
        fdOn = i("fxdist.enabled"); fdDrive = i("fxdist.drive"); fdTone = i("fxdist.tone"); fdMix = i("fxdist.mix");
        char b[32];
        for (int l = 0; l < 8; l++) {
            std::snprintf(b, sizeof b, "lfo%d.rate", l + 1); lfoRate[l] = i(b);
            std::snprintf(b, sizeof b, "lfo%d.sync", l + 1); lfoSync[l] = i(b);
            std::snprintf(b, sizeof b, "lfo%d.division", l + 1); lfoDiv[l] = i(b);
            std::snprintf(b, sizeof b, "lfo%d.phase", l + 1); lfoPhase[l] = i(b);
        }
        for (int m = 0; m < 4; m++) { std::snprintf(b, sizeof b, "macro%d.value", m + 1); macro[m] = i(b); }
        volume = i("master.volume"); bpm = i("master.bpm"); polyphony = i("master.polyphony"); bendRange = i("master.bend_range");
    }
};

const FxIdx& X() {
    static const FxIdx instance;
    return instance;
}

} // namespace

Processor::Processor(double sampleRate, std::uint32_t seed)
    : sr_(sampleRate), rng_(seed),
      chorus_(sampleRate), phaser_(sampleRate), flanger_(sampleRate), delay_(sampleRate),
      reverb_(sampleRate), eq_(sampleRate), comp_(sampleRate), fxdist_(sampleRate) {
    (void)X();
    defaultValues(base_);
    for (int i = 0; i < kMaxVoices; i++) voices_.push_back(std::make_unique<Voice>(sampleRate, kBlockSize));
    for (auto& s : lfoShapes_) s = defaultLfoShape();
    ctx_.sr = sampleRate;
    ctx_.blockSize = kBlockSize;
    ctx_.base = base_;
    ctx_.routes = &routes_;
    ctx_.lfoShapes = lfoShapes_;
    ctx_.lfoGlobalPhases = lfoGlobalPhases_;
    ctx_.lfoBeatPhases = lfoBeatPhases_;
    ctx_.lfoFreqs = lfoFreqs_;
    ctx_.rng = &rng_;
}

void Processor::setParam(int index, double value) noexcept {
    if (index >= 0 && index < kNumParams) base_[index] = static_cast<float>(value);
}

void Processor::sustain(bool down) noexcept {
    sustainDown_ = down;
    if (!down)
        for (auto& v : voices_)
            if (v->active() && v->sustained) v->noteOff();
}

void Processor::setModSlot(int slot, const std::optional<ModSlotState>& state) noexcept {
    if (slot < 0 || slot >= kMaxModSlots) return;
    slots_[slot] = state;
    rebuildRoutes();
}

void Processor::setLfoShape(int lfo, const LfoShape& shape) noexcept {
    if (lfo < 0 || lfo >= 8) return;
    lfoShapes_[lfo] = shape.count > 0 ? shape : defaultLfoShape();
}

void Processor::setFxOrder(const int* order, int count) noexcept {
    if (count == 8) std::copy(order, order + 8, fxOrder_);
}

void Processor::allNotesOff() noexcept {
    for (auto& v : voices_)
        if (v->active()) v->noteOff();
}

void Processor::rebuildRoutes() noexcept {
    routes_.clear();
    for (const auto& s : slots_) {
        if (!s || !s->enabled || s->depth == 0) continue;
        if (s->dest < 0 || s->dest >= kNumParams || !kParams[s->dest].moddable) continue;
        if (s->source < 0 || s->source >= static_cast<int>(kNumModSources)) continue;
        int d = 0;
        while (d < routes_.destCount && routes_.dests[d] != s->dest) d++;
        if (d == routes_.destCount) {
            routes_.dests[d] = s->dest;
            routes_.count[d] = 0;
            routes_.destCount++;
        }
        routes_.routes[d][routes_.count[d]++] = {s->source, s->depth};
    }
    for (auto& v : voices_) v->clearMods();
    std::memset(globalOffsets_, 0, sizeof globalOffsets_);
}

void Processor::noteOn(int note, double velocity) noexcept {
    const int poly = static_cast<int>(std::max(1.0, jsRound(normToValue(kParams[X().polyphony], base_[X().polyphony]))));
    Voice* voice = nullptr;
    int activeCount = 0;
    for (auto& v : voices_) if (v->active()) activeCount++;
    if (activeCount < poly)
        for (auto& v : voices_) if (!v->active()) { voice = v.get(); break; }
    if (!voice) {
        Voice* best = nullptr;
        for (auto& vp : voices_) {
            Voice* v = vp.get();
            if (!v->active()) { best = v; break; }
            if (v->releasing() && (!best || !best->releasing() || v->ampLevel() < best->ampLevel())) best = v;
        }
        if (!best || !best->releasing())
            for (auto& vp : voices_) {
                Voice* v = vp.get();
                if (v->active() && (!best || v->age < best->age)) best = v;
            }
        voice = best;
    }
    if (voice) voice->noteOn(note, std::max(0.001, velocity), noteAge_++, ctx_);
}

void Processor::noteOff(int note) noexcept {
    for (auto& v : voices_)
        if (v->active() && v->note == note && v->gate) {
            if (sustainDown_) v->sustained = true;
            else v->noteOff();
        }
}

void Processor::updateGlobalLfos(double bpm) noexcept {
    const double dt = kBlockSize / sr_;
    beatCounter_ += (bpm / 60) * dt;
    for (int l = 0; l < 8; l++) {
        const bool synced = base_[X().lfoSync[l]] >= 0.5f;
        double freq;
        if (synced) {
            const int div = static_cast<int>(jsRound(normToValue(kParams[X().lfoDiv[l]], base_[X().lfoDiv[l]])));
            freq = bpm / 60 / divisionToBeats(div);
        } else {
            freq = normToValue(kParams[X().lfoRate[l]], static_cast<double>(base_[X().lfoRate[l]]) + static_cast<double>(globalOffsets_[X().lfoRate[l]]));
        }
        lfoFreqs_[l] = static_cast<float>(freq);
        lfoGlobalPhases_[l] = static_cast<float>(jsMod(lfoGlobalPhases_[l] + freq * dt, 1));
        const double beatsPerCycle = synced
            ? divisionToBeats(static_cast<int>(jsRound(normToValue(kParams[X().lfoDiv[l]], base_[X().lfoDiv[l]]))))
            : std::max(60 / (freq * bpm), 1e-4);
        const double phase0 = base_[X().lfoPhase[l]];
        lfoBeatPhases_[l] = static_cast<float>(jsMod(beatCounter_ / beatsPerCycle + phase0, 1));
    }
}

void Processor::updateGlobalMods() noexcept {
    Voice* ref = nullptr;
    for (auto& v : voices_)
        if (v->active() && (!ref || v->age > ref->age)) ref = v.get();
    if (ref) std::copy(ref->sources, ref->sources + kNumModSources, globalSources_);
    else std::fill(globalSources_, globalSources_ + kNumModSources, 0.0f);
    for (int m = 0; m < 4; m++) globalSources_[kSrcMacro0 + m] = base_[X().macro[m]];
    globalSources_[kSrcModwheel] = static_cast<float>(modWheel_);
    globalSources_[kSrcPitchwheel] = static_cast<float>(pitchBend_);
    globalSources_[kSrcAftertouch] = static_cast<float>(aftertouch_);
    for (int d = 0; d < routes_.destCount; d++) {
        double sum = 0;
        for (int i = 0; i < routes_.count[d]; i++)
            sum += routes_.routes[d][i].depth * static_cast<double>(globalSources_[routes_.routes[d][i].source]);
        globalOffsets_[routes_.dests[d]] = static_cast<float>(sum);
    }
}

double Processor::gv(int index) const noexcept {
    return normToValue(kParams[index], static_cast<double>(base_[index]) + static_cast<double>(globalOffsets_[index]));
}

void Processor::runFx(float* l, float* r, int n, double bpm) noexcept {
    const FxIdx& p = X();
    for (int fx : fxOrder_) {
        switch (fx) {
        case 0: if (base_[p.chOn] >= 0.5f) chorus_.process(l, r, n, gv(p.chRate), gv(p.chDepth), gv(p.chMix)); break;
        case 1: if (base_[p.phOn] >= 0.5f) phaser_.process(l, r, n, gv(p.phRate), gv(p.phDepth), gv(p.phFb), gv(p.phMix)); break;
        case 2: if (base_[p.flOn] >= 0.5f) flanger_.process(l, r, n, gv(p.flRate), gv(p.flDepth), gv(p.flFb), gv(p.flMix)); break;
        case 3:
            if (base_[p.dlOn] >= 0.5f) {
                double time;
                if (base_[p.dlSync] >= 0.5f) {
                    const int div = static_cast<int>(jsRound(normToValue(kParams[p.dlDiv], base_[p.dlDiv])));
                    time = divisionToBeats(div) * (60 / bpm);
                } else time = gv(p.dlTime);
                delay_.process(l, r, n, time, gv(p.dlFb), base_[p.dlPp] >= 0.5f, gv(p.dlMix));
            }
            break;
        case 4: if (base_[p.rvOn] >= 0.5f) reverb_.process(l, r, n, gv(p.rvSize), gv(p.rvDamp), gv(p.rvWidth), gv(p.rvMix)); break;
        case 5: if (base_[p.eqOn] >= 0.5f) eq_.process(l, r, n, gv(p.eqLow), gv(p.eqMid), gv(p.eqMidF), gv(p.eqHigh)); break;
        case 6: if (base_[p.cpOn] >= 0.5f) comp_.process(l, r, n, gv(p.cpTh), gv(p.cpRatio), gv(p.cpAtk), gv(p.cpRel), gv(p.cpMk)); break;
        case 7: if (base_[p.fdOn] >= 0.5f) fxdist_.process(l, r, n, gv(p.fdDrive), gv(p.fdTone), gv(p.fdMix)); break;
        default: break;
        }
    }
}

int Processor::activeVoices() const noexcept {
    int c = 0;
    for (const auto& v : voices_) if (v->active()) c++;
    return c;
}

void Processor::process(float* l, float* r) noexcept {
    const int n = kBlockSize;
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);

    const double bpm = normToValue(kParams[X().bpm], base_[X().bpm]);
    updateGlobalLfos(bpm);

    ctx_.sample = sample_;
    ctx_.pitchBend = pitchBend_;
    ctx_.modWheel = modWheel_;
    ctx_.aftertouch = aftertouch_;
    ctx_.bendRange = jsRound(normToValue(kParams[X().bendRange], base_[X().bendRange]));
    for (int o = 0; o < 3; o++) ctx_.tables[o] = tables_[o];

    int voiceCount = 0;
    for (auto& v : voices_)
        if (v->active()) {
            v->render(ctx_, l, r, n);
            voiceCount++;
        }

    updateGlobalMods();
    runFx(l, r, n, bpm);

    const double vol = gv(X().volume);
    for (int i = 0; i < n; i++) {
        double sl = l[i] * vol;
        double sr = r[i] * vol;
        if (sl > 2) sl = 2; else if (sl < -2) sl = -2;
        if (sr > 2) sr = 2; else if (sr < -2) sr = -2;
        l[i] = static_cast<float>(sl);
        r[i] = static_cast<float>(sr);
        const double al = std::fabs(sl), ar = std::fabs(sr);
        if (al > peakL_) peakL_ = al;
        if (ar > peakR_) peakR_ = ar;
    }

    std::copy(l, l + n, scopeL_ + scopePos_);
    std::copy(r, r + n, scopeR_ + scopePos_);
    scopePos_ += n;
    if (scopePos_ >= kScopeSize) {
        scopePos_ = 0;
        std::copy(scopeL_, scopeL_ + kScopeSize, telemetry_.scopeL);
        std::copy(scopeR_, scopeR_ + kScopeSize, telemetry_.scopeR);
        telemetry_.voices = voiceCount;
        telemetry_.peakL = peakL_;
        telemetry_.peakR = peakR_;
        std::copy(globalSources_, globalSources_ + kNumModSources, telemetry_.sources);
        telemetryReady_ = true;
        peakL_ = 0;
        peakR_ = 0;
    }
}

} // namespace sg
