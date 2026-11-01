#include "sg/dsp.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sg {

double evalLfoShape(const LfoShape& s, double phase) noexcept {
    if (s.count == 0) return 0;
    const LfoPoint* p = s.points;
    if (phase <= p[0].x) return p[0].y;
    for (int i = 0; i < s.count - 1; i++) {
        const LfoPoint& a = p[i];
        const LfoPoint& b = p[i + 1];
        if (phase <= b.x) {
            const double span = b.x - a.x;
            if (span <= 1e-9) return b.y;
            double t = (phase - a.x) / span;
            const double pw = std::pow(2, a.power * 4);
            t = std::pow(t, pw);
            return a.y + (b.y - a.y) * t;
        }
    }
    return p[s.count - 1].y;
}

// ------------------------------------------------------------ wavetable

WavetableData::WavetableData(std::vector<float> mips, int frameSize, int numFrames)
    : mips_(std::move(mips)), frameSize_(frameSize), numFrames_(numFrames) {}

int WavetableData::mipForInc(double phaseInc) const noexcept {
    const double audible = 0.5 / std::max(phaseInc, 1e-9);
    int m = 0;
    while (m < kNumMips - 1 && (1024 >> m) > audible) m++;
    return m;
}

double WavetableData::read(double pos, double phase, int mip) const noexcept {
    const double fpos = pos * (numFrames_ - 1);
    int f0 = static_cast<int>(std::floor(fpos));
    if (f0 >= numFrames_ - 1) f0 = numFrames_ - 2;
    if (f0 < 0) f0 = 0;
    const double ft = numFrames_ > 1 ? fpos - f0 : 0;

    const int n = frameSize_;
    const double idx = phase * n;
    const int i0 = static_cast<int>(idx);
    const double st = idx - i0;
    const int i1 = i0 + 1 >= n ? 0 : i0 + 1;

    const float* a = this->mip(f0 * kNumMips + mip);
    const double a0 = a[i0], a1 = a[i1];
    const double s0 = a0 + (a1 - a0) * st;
    if (ft == 0 || numFrames_ == 1) return s0;
    const float* b = this->mip((f0 + 1) * kNumMips + mip);
    const double b0 = b[i0], b1 = b[i1];
    const double s1 = b0 + (b1 - b0) * st;
    return s0 + (s1 - s0) * ft;
}

// ------------------------------------------------------------ envelope

static double curveShape(double t, double c) noexcept { return std::pow(t, std::pow(2, c * 3)); }

void Envelope::gateOff() noexcept {
    if (stage_ != Stage::Idle && stage_ != Stage::Kill && stage_ != Stage::Release) {
        releaseFrom_ = level_;
        stage_ = Stage::Release;
        t_ = 0;
    }
}

void Envelope::kill() noexcept {
    if (stage_ != Stage::Idle) {
        releaseFrom_ = level_;
        stage_ = Stage::Kill;
        t_ = 0;
    }
}

double Envelope::process(const EnvParams& p) noexcept {
    t_ += dt_;
    switch (stage_) {
    case Stage::Idle: level_ = 0; break;
    case Stage::Delay:
        level_ = 0;
        if (t_ >= p.delay) { stage_ = Stage::Attack; t_ = 0; }
        break;
    case Stage::Attack:
        if (t_ >= p.attack) { level_ = 1; stage_ = Stage::Hold; t_ = 0; }
        else level_ = curveShape(t_ / p.attack, p.atkCurve);
        break;
    case Stage::Hold:
        level_ = 1;
        if (t_ >= p.hold) { stage_ = Stage::Decay; t_ = 0; }
        break;
    case Stage::Decay:
        if (t_ >= p.decay) { level_ = p.sustain; stage_ = Stage::Sustain; t_ = 0; }
        else level_ = p.sustain + (1 - p.sustain) * (1 - curveShape(t_ / p.decay, -p.decCurve));
        break;
    case Stage::Sustain: level_ = p.sustain; break;
    case Stage::Release:
        if (t_ >= p.release) { level_ = 0; stage_ = Stage::Idle; }
        else level_ = releaseFrom_ * (1 - curveShape(t_ / p.release, -p.relCurve));
        break;
    case Stage::Kill: {
        const double killTime = 0.004;
        if (t_ >= killTime) { level_ = 0; stage_ = Stage::Idle; }
        else level_ = releaseFrom_ * (1 - t_ / killTime);
        break;
    }
    }
    return level_;
}

// ------------------------------------------------------------ LFO

void Lfo::trigger(double startPhase) noexcept {
    phase = jsMod(startPhase, 1);
    smoothed_ = 0;
}

double Lfo::process(const LfoShape& points, double freq, int mode, double globalPhase, double beatPhase, double smooth) noexcept {
    double ph;
    if (mode == 1) ph = globalPhase;
    else if (mode == 2) ph = beatPhase;
    else {
        phase = jsMod(phase + freq * dt_, 1);
        ph = phase;
    }
    const double raw = evalLfoShape(points, ph);
    if (smooth <= 0.001) {
        smoothed_ = raw;
        return raw;
    }
    const double k = 1 - std::exp(-dt_ / (smooth * 0.25));
    smoothed_ += (raw - smoothed_) * k;
    return smoothed_;
}

// ------------------------------------------------------------ filters

double SvfChannel::process(double x, double g, double k, int mode) noexcept {
    const double a1 = 1 / (1 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1;
    ic2 = 2 * v2 - ic2;
    const double lp = v2, bp = v1, hp = x - k * v1 - v2;
    switch (mode) {
    case 0: return lp;
    case 1: return hp;
    case 2: return bp * k;
    default: return x - k * bp;
    }
}

void VoiceFilter::reset() noexcept {
    for (auto& c : s1_) c.reset();
    for (auto& c : s2_) c.reset();
    for (auto& row : formant_) for (auto& c : row) c.reset();
    std::memset(comb_, 0, sizeof(comb_));
    combLp_[0] = combLp_[1] = 0;
    combPos_ = 0;
}

void VoiceFilter::process(float* l, float* r, int n, int type, double cutoff, double res, double drive, double mix) noexcept {
    const double driveGain = 1 + drive * 9;
    const double driveComp = 1 / std::sqrt(driveGain);
    const double wet = mix;
    const double dry = 1 - mix;

    if (type <= 6) {
        const int mode = type <= 1 ? 0 : type <= 3 ? 1 : type <= 5 ? 2 : 3;
        const bool twoPole = type == 1 || type == 3 || type == 5;
        const double fc = std::min(std::max(cutoff, 10.0), sr_ * 0.49);
        const double g = std::tan((kPi * fc) / sr_);
        const double k = 2 - 1.98 * std::min(res, 0.99);
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? l : r;
            SvfChannel& f1 = s1_[ch];
            SvfChannel& f2 = s2_[ch];
            for (int i = 0; i < n; i++) {
                const double in = buf[i];
                const double x = drive > 0.001 ? std::tanh(in * driveGain) * driveComp : in;
                double y = f1.process(x, g, k, mode);
                if (twoPole) y = f2.process(y, g, std::max(k, 1.0), mode);
                buf[i] = static_cast<float>(dry * in + wet * y);
            }
        }
        return;
    }

    if (type == 7) {
        const double delay = std::min(static_cast<double>(kCombSize - 2), std::max(2.0, sr_ / std::max(cutoff, 20.0)));
        const double fb = 0.5 + res * 0.48;
        for (int i = 0; i < n; i++) {
            const double readPos = jsMod(combPos_ - delay + kCombSize, kCombSize);
            const int ri = static_cast<int>(readPos);
            const double rf = readPos - ri;
            for (int ch = 0; ch < 2; ch++) {
                float* buf = ch == 0 ? l : r;
                float* cb = comb_[ch];
                const double c0 = cb[ri], c1 = cb[(ri + 1) % kCombSize];
                const double dl = c0 + (c1 - c0) * rf;
                combLp_[ch] += (dl - combLp_[ch]) * 0.6;
                const double in = buf[i];
                const double x = drive > 0.001 ? std::tanh(in * driveGain) * driveComp : in;
                const double y = x + combLp_[ch] * fb;
                cb[combPos_] = static_cast<float>(y);
                buf[i] = static_cast<float>(dry * in + wet * y * 0.5);
            }
            combPos_ = (combPos_ + 1) % kCombSize;
        }
        return;
    }

    static const double vowels[5][3] = {
        {800, 1150, 2900}, {400, 2000, 2800}, {250, 2300, 3000}, {400, 800, 2600}, {350, 600, 2700}};
    const double norm = std::min(std::max(std::log(cutoff / 20) / std::log(1000.0), 0.0), 1.0) * (5 - 1);
    const int v0 = std::min(static_cast<int>(std::floor(norm)), 5 - 2);
    const double vt = norm - v0;
    const double k = 2 - 1.9 * std::min(0.3 + res * 0.7, 0.99);
    static const double gains[3] = {1, 0.6, 0.35};
    for (int b = 0; b < 3; b++) {
        const double f = vowels[v0][b] + (vowels[v0 + 1][b] - vowels[v0][b]) * vt;
        const double g = std::tan((kPi * std::min(f, sr_ * 0.45)) / sr_);
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? l : r;
            SvfChannel& svf = formant_[ch][b];
            for (int i = 0; i < n; i++) {
                const double in = buf[i];
                const double x = b == 0 ? (drive > 0.001 ? std::tanh(in * driveGain) * driveComp : in) : in;
                const double y = svf.process(x, g, k, 2) * gains[b] * 2.5;
                if (b == 0) buf[i] = static_cast<float>(dry * in + wet * y);
                else buf[i] = static_cast<float>(in + wet * y);
            }
        }
    }
}

double noteToFreq(double note) noexcept { return 440 * std::pow(2, (note - 69) / 12); }

} // namespace sg
