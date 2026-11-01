#pragma once

// Port of the reference src/worklet/dsp.ts.

#include "sg/messages.h"
#include "sg/wavetable_gen.h"

#include <vector>

namespace sg {

/** Band-limited wavetable: per frame, kNumMips low-passed copies. */
class WavetableData {
public:
    WavetableData(std::vector<float> mips, int frameSize, int numFrames);

    int mipForInc(double phaseInc) const noexcept;
    double read(double pos, double phase, int mip) const noexcept;

    int frameSize() const noexcept { return frameSize_; }
    int numFrames() const noexcept { return numFrames_; }
    const float* mip(int i) const noexcept { return mips_.data() + static_cast<std::size_t>(i) * frameSize_; }

private:
    std::vector<float> mips_;
    int frameSize_;
    int numFrames_;
};

struct EnvParams {
    double delay = 0, attack = 0, hold = 0, decay = 0, sustain = 0, release = 0;
    double atkCurve = 0, decCurve = 0, relCurve = 0;
};

class Envelope {
public:
    Envelope(double sr, int blockSize) : dt_(blockSize / sr) {}
    void trigger() noexcept { stage_ = Stage::Delay; t_ = 0; }
    void gateOff() noexcept;
    void kill() noexcept;
    bool idle() const noexcept { return stage_ == Stage::Idle; }
    bool releasing() const noexcept { return stage_ == Stage::Release || stage_ == Stage::Kill; }
    double value() const noexcept { return level_; }
    double process(const EnvParams& p) noexcept;

private:
    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release, Kill };
    Stage stage_ = Stage::Idle;
    double t_ = 0;
    double level_ = 0;
    double releaseFrom_ = 0;
    double dt_;
};

class Lfo {
public:
    Lfo(double sr, int blockSize) : dt_(blockSize / sr) {}
    void trigger(double startPhase) noexcept;
    double process(const LfoShape& points, double freq, int mode, double globalPhase, double beatPhase, double smooth) noexcept;
    double phase = 0;

private:
    double smoothed_ = 0;
    double dt_;
};

struct SvfChannel {
    double ic1 = 0, ic2 = 0;
    double process(double x, double g, double k, int mode) noexcept;
    void reset() noexcept { ic1 = ic2 = 0; }
};

class VoiceFilter {
public:
    static constexpr int kCombSize = 4096;
    explicit VoiceFilter(double sr) : sr_(sr) {}
    void reset() noexcept;
    void process(float* l, float* r, int n, int type, double cutoff, double res, double drive, double mix) noexcept;

private:
    SvfChannel s1_[2], s2_[2];
    SvfChannel formant_[2][3];
    float comb_[2][kCombSize] = {};
    int combPos_ = 0;
    double combLp_[2] = {0, 0};
    double sr_;
};

double noteToFreq(double note) noexcept;

} // namespace sg
