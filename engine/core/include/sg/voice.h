#pragma once

// Port of the reference src/worklet/voice.ts.

#include "sg/dsp.h"
#include "sg/messages.h"
#include "sg/params.h"
#include "sg/rng.h"

namespace sg {

/** routesByDest as fixed arrays, iterated in first-seen dest order like the JS Map. */
struct ModRoutes {
    struct Route { int source; double depth; };
    int destCount = 0;
    int dests[kMaxModSlots] = {};
    int count[kMaxModSlots] = {};
    Route routes[kMaxModSlots][kMaxModSlots] = {};
    void clear() noexcept { destCount = 0; }
};

struct Sample {
    std::vector<float> data;
    int sampleRate = 0;
};

struct VoiceContext {
    double sr = 48000;
    int blockSize = kBlockSize;
    const float* base = nullptr;
    const ModRoutes* routes = nullptr;
    const WavetableData* tables[3] = {nullptr, nullptr, nullptr};
    const LfoShape* lfoShapes = nullptr;  // 8
    const Sample* sample = nullptr;
    const float* lfoGlobalPhases = nullptr;  // 8
    const float* lfoBeatPhases = nullptr;    // 8
    const float* lfoFreqs = nullptr;         // 8
    double pitchBend = 0;
    double modWheel = 0;
    double aftertouch = 0;
    double bendRange = 2;
    Rng* rng = nullptr;
};

class Voice {
public:
    Voice(double sr, int blockSize);

    bool active() const noexcept { return !envs_[0].idle(); }
    bool releasing() const noexcept { return envs_[0].releasing(); }
    double ampLevel() const noexcept { return envs_[0].value(); }

    void noteOn(int note, double velocity, double age, const VoiceContext& ctx) noexcept;
    void noteOff() noexcept;
    void clearMods() noexcept;
    void kill() noexcept;
    bool render(const VoiceContext& ctx, float* outL, float* outR, int n) noexcept;

    int note = 60;
    double velocity = 1;
    bool gate = false;
    bool sustained = false;
    double age = 0;
    float sources[kNumModSources] = {};

private:
    double pv(int index, const VoiceContext& ctx) const noexcept;
    void computeMods(const VoiceContext& ctx) noexcept;
    void advanceModulators(const VoiceContext& ctx) noexcept;
    void runFilter(int f, float* l, float* r, int n, const VoiceContext& ctx) noexcept;

    double sr_;
    float modOffsets_[kNumParams] = {};
    double phases_[3 * kMaxUnison] = {};
    double subPhase_ = 0;
    double samplePos_ = 0;
    double random_ = 0;
    double prevAmp_ = 0;
    Envelope envs_[6];
    Lfo lfos_[8];
    VoiceFilter filters_[2];
    float bufL_[kBlockSize] = {}, bufR_[kBlockSize] = {}, tmpL_[kBlockSize] = {}, tmpR_[kBlockSize] = {};
    double distHoldL_ = 0, distHoldR_ = 0, distCount_ = 0;
    double pink_[3] = {0, 0, 0};
};

} // namespace sg
