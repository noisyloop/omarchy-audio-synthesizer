#include "sg/effects.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>

namespace sg {

void ModDelayLine::write(double x) noexcept {
    buf[static_cast<std::size_t>(pos)] = static_cast<float>(x);
    pos = (pos + 1) % static_cast<int>(buf.size());
}

double ModDelayLine::read(double delay) const noexcept {
    const int size = static_cast<int>(buf.size());
    double p = pos - delay;
    while (p < 0) p += size;
    const int i0 = static_cast<int>(p);
    const double frac = p - i0;
    const int i1 = (i0 + 1) % size;
    const double a = buf[static_cast<std::size_t>(i0)], b = buf[static_cast<std::size_t>(i1)];
    return a + (b - a) * frac;
}

void Chorus::process(float* l, float* r, int n, double rate, double depth, double mix) noexcept {
    const double base = 0.02 * sr_;
    const double dep = depth * 0.008 * sr_;
    const double inc = rate / sr_;
    for (int i = 0; i < n; i++) {
        phase_ = jsMod(phase_ + inc, 1);
        const double lfoL = std::sin(phase_ * 2 * kPi);
        const double lfoR = std::sin((phase_ + 0.25) * 2 * kPi);
        dl_[0].write(l[i]);
        dl_[1].write(r[i]);
        const double wl = dl_[0].read(base + dep * (1 + lfoL) + 1);
        const double wr = dl_[1].read(base + dep * (1 + lfoR) + 1);
        l[i] = static_cast<float>(l[i] * (1 - mix) + wl * mix);
        r[i] = static_cast<float>(r[i] * (1 - mix) + wr * mix);
    }
}

void Flanger::process(float* l, float* r, int n, double rate, double depth, double feedback, double mix) noexcept {
    const double mn = 0.001 * sr_;
    const double dep = depth * 0.006 * sr_;
    const double inc = rate / sr_;
    for (int i = 0; i < n; i++) {
        phase_ = jsMod(phase_ + inc, 1);
        const double halfTri = (1 - std::cos(phase_ * 2 * kPi)) * 0.5;
        const double d = mn + dep * halfTri + 1;
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? l : r;
            dl_[ch].write(buf[i] + fb_[ch] * feedback);
            const double w = dl_[ch].read(d);
            fb_[ch] = flush(w);
            buf[i] = static_cast<float>(buf[i] * (1 - mix) + w * mix);
        }
    }
}

void Phaser::process(float* l, float* r, int n, double rate, double depth, double feedback, double mix) noexcept {
    const double inc = rate / sr_;
    for (int i = 0; i < n; i++) {
        phase_ = jsMod(phase_ + inc, 1);
        for (int ch = 0; ch < 2; ch++) {
            const double sweep = (1 - std::cos((phase_ + ch * 0.25) * 2 * kPi)) * 0.5;
            const double f = 300 * std::pow(2, sweep * depth * 4.5);
            const double w = std::min((kPi * f) / sr_, 1.5);
            const double a = (1 - std::tan(w)) / (1 + std::tan(w));
            float* buf = ch == 0 ? l : r;
            double x = buf[i] + fb_[ch] * feedback;
            double* st = ap_[ch];
            for (int s = 0; s < 6; s++) {
                const double y = a * x + st[s];
                st[s] = x - a * y;
                x = y;
            }
            fb_[ch] = flush(x);
            buf[i] = static_cast<float>(buf[i] * (1 - mix) + x * mix);
        }
    }
}

StereoDelay::StereoDelay(double sr) : smoothedDelay_(sr * 0.35), sr_(sr) {
    const int size = static_cast<int>(std::ceil(sr * 2.5));
    dl_.emplace_back(size);
    dl_.emplace_back(size);
}

void StereoDelay::process(float* l, float* r, int n, double timeSec, double feedback, bool pingpong, double mix) noexcept {
    const double target = std::min(std::max(timeSec * sr_, 32.0), sr_ * 2.4);
    for (int i = 0; i < n; i++) {
        smoothedDelay_ += (target - smoothedDelay_) * 0.0005;
        const double d = smoothedDelay_;
        const double wl = dl_[0].read(d);
        const double wr = dl_[1].read(d);
        const double xl = l[i], xr = r[i];
        if (pingpong) {
            dl_[0].write(flush(xl * 0.5 + xr * 0.5 + wr * feedback));
            dl_[1].write(flush(wl * feedback));
        } else {
            dl_[0].write(flush(xl + wl * feedback));
            dl_[1].write(flush(xr + wr * feedback));
        }
        l[i] = static_cast<float>(xl * (1 - mix * 0.5) + wl * mix);
        r[i] = static_cast<float>(xr * (1 - mix * 0.5) + wr * mix);
    }
}

double Reverb::Comb::process(double x, double feedback, double damp) noexcept {
    const double out = buf[static_cast<std::size_t>(pos)];
    filterStore = flush(out * (1 - damp) + filterStore * damp);
    buf[static_cast<std::size_t>(pos)] = static_cast<float>(flush(x + filterStore * feedback));
    pos = (pos + 1) % static_cast<int>(buf.size());
    return out;
}

double Reverb::Allpass::process(double x) noexcept {
    const double bufOut = buf[static_cast<std::size_t>(pos)];
    const double out = -x + bufOut;
    buf[static_cast<std::size_t>(pos)] = static_cast<float>(flush(x + bufOut * 0.5));
    pos = (pos + 1) % static_cast<int>(buf.size());
    return out;
}

Reverb::Reverb(double sr) {
    static const int combT[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int apT[] = {556, 441, 341, 225};
    const int spread = 23;
    const double scale = sr / 44100;
    for (int ch = 0; ch < 2; ch++) {
        for (int t : combT) combs_[ch].push_back(Comb{std::vector<float>(static_cast<std::size_t>(jsRound((t + ch * spread) * scale)), 0.0f)});
        for (int t : apT) allpasses_[ch].push_back(Allpass{std::vector<float>(static_cast<std::size_t>(jsRound((t + ch * spread) * scale)), 0.0f)});
    }
}

void Reverb::process(float* l, float* r, int n, double size, double damp, double width, double mix) noexcept {
    const double feedback = 0.7 + size * 0.28;
    const double dampC = damp * 0.4;
    const double wet1 = (mix * (1 + width)) / 2;
    const double wet2 = (mix * (1 - width)) / 2;
    for (int i = 0; i < n; i++) {
        const double input = (static_cast<double>(l[i]) + r[i]) * 0.015;
        double outL = 0, outR = 0;
        for (int c = 0; c < 8; c++) {
            outL += combs_[0][static_cast<std::size_t>(c)].process(input, feedback, dampC);
            outR += combs_[1][static_cast<std::size_t>(c)].process(input, feedback, dampC);
        }
        for (int a = 0; a < 4; a++) {
            outL = allpasses_[0][static_cast<std::size_t>(a)].process(outL);
            outR = allpasses_[1][static_cast<std::size_t>(a)].process(outR);
        }
        l[i] = static_cast<float>(l[i] * (1 - mix) + outL * wet1 + outR * wet2);
        r[i] = static_cast<float>(r[i] * (1 - mix) + outR * wet1 + outL * wet2);
    }
}

void Eq3::Biquad::lowShelf(double sr, double f, double gainDb) noexcept {
    const double A = std::pow(10, gainDb / 40);
    const double w = (2 * kPi * f) / sr;
    const double cs = std::cos(w), sn = std::sin(w);
    const double beta = std::sqrt(A) / 0.9;
    const double nb0 = A * (A + 1 - (A - 1) * cs + beta * sn);
    const double nb1 = 2 * A * (A - 1 - (A + 1) * cs);
    const double nb2 = A * (A + 1 - (A - 1) * cs - beta * sn);
    const double a0 = A + 1 + (A - 1) * cs + beta * sn;
    const double na1 = -2 * (A - 1 + (A + 1) * cs);
    const double na2 = A + 1 + (A - 1) * cs - beta * sn;
    b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
}

void Eq3::Biquad::highShelf(double sr, double f, double gainDb) noexcept {
    const double A = std::pow(10, gainDb / 40);
    const double w = (2 * kPi * f) / sr;
    const double cs = std::cos(w), sn = std::sin(w);
    const double beta = std::sqrt(A) / 0.9;
    const double nb0 = A * (A + 1 + (A - 1) * cs + beta * sn);
    const double nb1 = -2 * A * (A - 1 + (A + 1) * cs);
    const double nb2 = A * (A + 1 + (A - 1) * cs - beta * sn);
    const double a0 = A + 1 - (A - 1) * cs + beta * sn;
    const double na1 = 2 * (A - 1 - (A + 1) * cs);
    const double na2 = A + 1 - (A - 1) * cs - beta * sn;
    b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
}

void Eq3::Biquad::peak(double sr, double f, double gainDb, double q) noexcept {
    const double A = std::pow(10, gainDb / 40);
    const double w = (2 * kPi * f) / sr;
    const double alpha = std::sin(w) / (2 * q);
    const double cs = std::cos(w);
    const double nb0 = 1 + alpha * A, nb1 = -2 * cs, nb2 = 1 - alpha * A;
    const double a0 = 1 + alpha / A, na1 = -2 * cs, na2 = 1 - alpha / A;
    b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
}

double Eq3::Biquad::processCh(double x, int ch) noexcept {
    const double y = b0 * x + b1 * x1[ch] + b2 * x2[ch] - a1 * y1[ch] - a2 * y2[ch];
    x2[ch] = x1[ch];
    x1[ch] = x;
    y2[ch] = y1[ch];
    y1[ch] = flush(y);
    return y;
}

void Eq3::process(float* l, float* r, int n, double lowDb, double midDb, double midFreq, double highDb) noexcept {
    low_.lowShelf(sr_, 250, lowDb);
    mid_.peak(sr_, midFreq, midDb, 0.7);
    high_.highShelf(sr_, 4000, highDb);
    for (int i = 0; i < n; i++) {
        l[i] = static_cast<float>(high_.processCh(mid_.processCh(low_.processCh(l[i], 0), 0), 0));
        r[i] = static_cast<float>(high_.processCh(mid_.processCh(low_.processCh(r[i], 1), 1), 1));
    }
}

void Compressor::process(float* l, float* r, int n, double thresholdDb, double ratio, double attack, double release, double makeupDb) noexcept {
    const double atkC = std::exp(-1 / (attack * sr_));
    const double relC = std::exp(-1 / (release * sr_));
    const double makeup = std::pow(10, makeupDb / 20);
    for (int i = 0; i < n; i++) {
        const double peak = std::max(std::fabs(static_cast<double>(l[i])), std::fabs(static_cast<double>(r[i])));
        const double coeff = peak > env_ ? atkC : relC;
        env_ = flush(coeff * env_ + (1 - coeff) * peak);
        const double envDb = 20 * std::log10(std::max(env_, 1e-6));
        double gainDb = 0;
        if (envDb > thresholdDb) gainDb = (thresholdDb - envDb) * (1 - 1 / ratio);
        const double g = std::pow(10, gainDb / 20) * makeup;
        l[i] = static_cast<float>(l[i] * g);
        r[i] = static_cast<float>(r[i] * g);
    }
}

void FxDistortion::process(float* l, float* r, int n, double drive, double tone, double mix) noexcept {
    const double gain = 1 + drive * 30;
    const double comp = 1 / std::pow(gain, 0.6);
    const double k = 1 - std::exp((-2 * kPi * tone) / sr_);
    for (int i = 0; i < n; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? l : r;
            const double x = buf[i];
            const double shaped = std::tanh(x * gain) * comp;
            lp_[ch] += (shaped - lp_[ch]) * k;
            buf[i] = static_cast<float>(x * (1 - mix) + lp_[ch] * mix);
        }
    }
}

} // namespace sg
