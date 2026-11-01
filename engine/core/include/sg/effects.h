#pragma once

// Port of the reference src/worklet/effects.ts.

#include <vector>

namespace sg {

class ModDelayLine {
public:
    explicit ModDelayLine(int size) : buf(static_cast<std::size_t>(size), 0.0f) {}
    void write(double x) noexcept;
    double read(double delay) const noexcept;
    std::vector<float> buf;
    int pos = 0;
};

class Chorus {
public:
    explicit Chorus(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double rate, double depth, double mix) noexcept;
private:
    ModDelayLine dl_[2]{ModDelayLine(8192), ModDelayLine(8192)};
    double phase_ = 0;
    double sr_;
};

class Flanger {
public:
    explicit Flanger(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double rate, double depth, double feedback, double mix) noexcept;
private:
    ModDelayLine dl_[2]{ModDelayLine(4096), ModDelayLine(4096)};
    double fb_[2] = {0, 0};
    double phase_ = 0;
    double sr_;
};

class Phaser {
public:
    explicit Phaser(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double rate, double depth, double feedback, double mix) noexcept;
private:
    double ap_[2][6] = {};
    double fb_[2] = {0, 0};
    double phase_ = 0;
    double sr_;
};

class StereoDelay {
public:
    explicit StereoDelay(double sr);
    void process(float* l, float* r, int n, double timeSec, double feedback, bool pingpong, double mix) noexcept;
private:
    std::vector<ModDelayLine> dl_;
    double smoothedDelay_;
    double sr_;
};

class Reverb {
public:
    explicit Reverb(double sr);
    void process(float* l, float* r, int n, double size, double damp, double width, double mix) noexcept;
private:
    struct Comb { std::vector<float> buf; int pos = 0; double filterStore = 0; double process(double x, double fb, double damp) noexcept; };
    struct Allpass { std::vector<float> buf; int pos = 0; double process(double x) noexcept; };
    std::vector<Comb> combs_[2];
    std::vector<Allpass> allpasses_[2];
};

class Eq3 {
public:
    explicit Eq3(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double lowDb, double midDb, double midFreq, double highDb) noexcept;
private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
        void lowShelf(double sr, double f, double gainDb) noexcept;
        void highShelf(double sr, double f, double gainDb) noexcept;
        void peak(double sr, double f, double gainDb, double q) noexcept;
        double processCh(double x, int ch) noexcept;
    };
    Biquad low_, mid_, high_;
    double sr_;
};

class Compressor {
public:
    explicit Compressor(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double thresholdDb, double ratio, double attack, double release, double makeupDb) noexcept;
private:
    double env_ = 0;
    double sr_;
};

class FxDistortion {
public:
    explicit FxDistortion(double sr) : sr_(sr) {}
    void process(float* l, float* r, int n, double drive, double tone, double mix) noexcept;
private:
    double lp_[2] = {0, 0};
    double sr_;
};

} // namespace sg
