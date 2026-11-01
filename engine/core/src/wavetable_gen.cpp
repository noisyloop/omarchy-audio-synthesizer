#include "sg/wavetable_gen.h"

#include "sg/fft.h"
#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace sg {

std::vector<float> buildMips(const float* data, int frameSize, int numFrames) {
    std::vector<float> out(static_cast<std::size_t>(numFrames) * kNumMips * frameSize);
    for (int f = 0; f < numFrames; f++) {
        const float* cycle = data + static_cast<std::size_t>(f) * frameSize;
        for (int m = 0; m < kNumMips; m++) {
            float* dst = out.data() + (static_cast<std::size_t>(f) * kNumMips + m) * frameSize;
            if (m == 0) std::memcpy(dst, cycle, sizeof(float) * frameSize);
            else {
                auto bl = bandlimitCycle(cycle, frameSize, std::max(1, (frameSize >> 1) >> m));
                std::memcpy(dst, bl.data(), sizeof(float) * frameSize);
            }
        }
    }
    return out;
}

static void normalizeTable(std::vector<float>& data) {
    double peak = 0;
    for (float v : data) peak = std::max(peak, std::fabs(static_cast<double>(v)));
    if (peak > 1e-9) {
        const double g = 1 / peak;
        for (float& v : data) v = static_cast<float>(v * g);
    }
}

std::vector<float> spectralMorph(const std::vector<std::vector<float>>& keyFrames, int numFrames) {
    const int n = kFrameSize;
    const int half = n >> 1;
    struct Spec { std::vector<float> mag, phase; };
    std::vector<Spec> specs;
    for (const auto& f : keyFrames) {
        std::vector<float> re(f), im(n, 0.0f);
        fft(re.data(), im.data(), n);
        Spec s{std::vector<float>(half), std::vector<float>(half)};
        for (int k = 0; k < half; k++) {
            s.mag[k] = static_cast<float>(std::hypot(static_cast<double>(re[k]), static_cast<double>(im[k])));
            s.phase[k] = static_cast<float>(std::atan2(static_cast<double>(im[k]), static_cast<double>(re[k])));
        }
        specs.push_back(std::move(s));
    }
    const int ns = static_cast<int>(specs.size());
    std::vector<float> out(static_cast<std::size_t>(numFrames) * n);
    std::vector<float> re(n), im(n);
    for (int f = 0; f < numFrames; f++) {
        const double pos = numFrames == 1 ? 0 : (static_cast<double>(f) / (numFrames - 1)) * (ns - 1);
        const int i0 = std::min(static_cast<int>(std::floor(pos)), ns - 1);
        const int i1 = std::min(i0 + 1, ns - 1);
        const double t = pos - i0;
        std::fill(re.begin(), re.end(), 0.0f);
        std::fill(im.begin(), im.end(), 0.0f);
        for (int k = 1; k < half; k++) {
            const double m0 = specs[i0].mag[k], m1 = specs[i1].mag[k];
            const double m = m0 + (m1 - m0) * t;
            const double p0 = specs[i0].phase[k];
            const double p1 = specs[i1].phase[k];
            double dp = p1 - p0;
            if (dp > kPi) dp -= 2 * kPi;
            if (dp < -kPi) dp += 2 * kPi;
            const double ph = p0 + dp * t;
            re[k] = static_cast<float>(m * std::cos(ph));
            im[k] = static_cast<float>(m * std::sin(ph));
            re[n - k] = re[k];
            im[n - k] = -im[k];
        }
        ifft(re.data(), im.data(), n);
        std::copy(re.begin(), re.end(), out.begin() + static_cast<std::ptrdiff_t>(f) * n);
    }
    normalizeTable(out);
    return out;
}

// ------------------------------------------------------------ key-frame recipes

static std::vector<float> additive(const std::function<double(int)>& harmonics, int count = 512) {
    const int n = kFrameSize;
    std::vector<float> re(n, 0.0f), im(n, 0.0f);
    const int half = n >> 1;
    for (int k = 1; k <= std::min(count, half - 1); k++) {
        const double a = harmonics(k);
        if (a == 0) continue;
        im[k] = static_cast<float>((-a * n) / 2);
        im[n - k] = static_cast<float>((a * n) / 2);
    }
    ifft(re.data(), im.data(), n);
    return re;
}

static std::vector<float> sineFrame() { return additive([](int k) { return k == 1 ? 1.0 : 0.0; }); }
static std::vector<float> triFrame() {
    return additive([](int k) {
        return k % 2 == 1 ? ((k % 4 == 1 ? 1 : -1) * 8) / (kPi * kPi * k * k) : 0.0;
    });
}
static std::vector<float> sawFrame() { return additive([](int k) { return (2 / kPi) * (1.0 / k); }, 800); }
static std::vector<float> squareFrame() {
    return additive([](int k) { return k % 2 == 1 ? 4 / (kPi * k) : 0.0; }, 800);
}
static std::vector<float> pwmFrame(double width) {
    return additive([width](int k) { return ((2 / (kPi * k)) * std::sin(kPi * k * width)) * 2; }, 600);
}
static std::vector<float> fmFrame(double index, double ratio) {
    const int n = kFrameSize;
    std::vector<float> out(n);
    for (int i = 0; i < n; i++) {
        const double ph = (static_cast<double>(i) / n) * 2 * kPi;
        out[i] = static_cast<float>(std::sin(ph + index * std::sin(ratio * ph)));
    }
    return out;
}
static std::vector<float> vocalFrame(double f1, double f2, double f3) {
    return additive([=](int k) {
        auto peak = [k](double c, double w) { return std::exp(-((k - c) * (k - c)) / (2 * w * w)); };
        return (1 / std::sqrt(static_cast<double>(k))) * (peak(f1, 2) + 0.7 * peak(f2, 3) + 0.4 * peak(f3, 4));
    }, 128);
}
static std::vector<float> digitalFrame(std::uint32_t seed) {
    const int n = kFrameSize;
    std::vector<float> out(n);
    std::uint32_t s = seed;
    auto rand = [&s]() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s) / 4294967295.0) * 2 - 1;
    };
    const int steps = 8 + static_cast<int>(seed % 24);
    std::vector<double> levels;
    for (int i = 0; i < steps; i++) levels.push_back(rand());
    for (int i = 0; i < n; i++)
        out[i] = static_cast<float>(levels[static_cast<std::size_t>(std::floor((static_cast<double>(i) / n) * steps))]);
    double mean = 0;
    for (int i = 0; i < n; i++) mean += out[i];
    mean /= n;
    for (int i = 0; i < n; i++) out[i] = static_cast<float>(out[i] - mean);
    return out;
}

Wavetable generateWavetable(const std::string& name) {
    Wavetable t;
    t.name = name;
    int numFrames = 32;
    if (name == "Basic Shapes") {
        t.data = spectralMorph({sineFrame(), triFrame(), sawFrame(), squareFrame()}, numFrames);
    } else if (name == "Harmonic Sweep") {
        std::vector<std::vector<float>> keys;
        for (int h : {1, 2, 4, 8, 16, 32})
            keys.push_back(additive([h](int k) { return k <= h ? 1 / std::sqrt(static_cast<double>(k)) : 0.0; }, 64));
        t.data = spectralMorph(keys, numFrames);
    } else if (name == "PWM") {
        t.data.assign(static_cast<std::size_t>(numFrames) * kFrameSize, 0.0f);
        for (int f = 0; f < numFrames; f++) {
            const double width = 0.5 - (static_cast<double>(f) / (numFrames - 1)) * 0.45;
            auto fr = pwmFrame(width);
            std::copy(fr.begin(), fr.end(), t.data.begin() + static_cast<std::ptrdiff_t>(f) * kFrameSize);
        }
        normalizeTable(t.data);
    } else if (name == "Vocal") {
        t.data = spectralMorph({vocalFrame(6, 9, 22), vocalFrame(4, 16, 24), vocalFrame(2, 20, 28),
                                vocalFrame(3, 7, 21), vocalFrame(2, 6, 18)}, numFrames);
    } else if (name == "FM Bell") {
        t.data = spectralMorph({fmFrame(0.5, 2), fmFrame(2, 2), fmFrame(4, 3.01), fmFrame(7, 5)}, numFrames);
    } else if (name == "Digital") {
        t.data = spectralMorph({digitalFrame(7), digitalFrame(1234), digitalFrame(9876), digitalFrame(31415)}, numFrames);
    } else {
        t.data = spectralMorph({sineFrame(), sawFrame()}, numFrames);
    }
    t.numFrames = numFrames;
    return t;
}

// ------------------------------------------------------------ WAV import

DecodedWav decodeWav(const std::uint8_t* b, std::size_t size) {
    auto u16 = [&](std::size_t o) { return static_cast<unsigned>(b[o] | (b[o + 1] << 8)); };
    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
    };
    auto tag = [&](std::size_t o, const char* t) { return std::memcmp(b + o, t, 4) == 0; };
    if (size < 12 || !tag(0, "RIFF") || !tag(8, "WAVE")) throw std::runtime_error("not a WAV file");
    unsigned format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    bool haveFmt = false;
    std::size_t dataOff = 0, dataLen = 0;
    bool haveData = false;
    std::size_t off = 12;
    while (off + 8 <= size) {
        const std::uint32_t sz = u32(off + 4);
        if (tag(off, "fmt ") && off + 24 <= size) {
            format = u16(off + 8); channels = u16(off + 10); rate = u32(off + 12); bits = u16(off + 22);
            haveFmt = true;
        } else if (tag(off, "data")) {
            dataOff = off + 8;
            dataLen = std::min<std::size_t>(sz, size - dataOff);
            haveData = true;
        }
        off += 8 + static_cast<std::size_t>(sz) + (sz & 1);
    }
    if (!haveFmt || !haveData || channels == 0 || bits == 0) throw std::runtime_error("malformed WAV (missing fmt/data chunk)");
    const std::size_t bytesPer = bits / 8;
    const std::size_t frames = dataLen / (bytesPer * channels);
    DecodedWav out;
    out.sampleRate = static_cast<int>(rate);
    out.channelData.resize(frames);
    for (std::size_t i = 0; i < frames; i++) {
        double sum = 0;
        for (unsigned c = 0; c < channels; c++) {
            const std::size_t o = dataOff + (i * channels + c) * bytesPer;
            double v;
            if (format == 3 && bits == 32) { float f; std::memcpy(&f, b + o, 4); v = f; }
            else if (bits == 16) v = static_cast<std::int16_t>(u16(o)) / 32768.0;
            else if (bits == 24) {
                int x = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16);
                if (x & 0x800000) x -= 0x1000000;
                v = x / 8388608.0;
            } else if (bits == 32) v = static_cast<std::int32_t>(u32(o)) / 2147483648.0;
            else if (bits == 8) v = (b[o] - 128) / 128.0;
            else throw std::runtime_error("unsupported WAV bit depth");
            sum += v;
        }
        out.channelData[i] = static_cast<float>(sum / channels);
    }
    return out;
}

Wavetable wavToWavetable(const std::string& name, const DecodedWav& wav) {
    const auto& d = wav.channelData;
    Wavetable t;
    t.name = name;
    if (d.size() >= static_cast<std::size_t>(kFrameSize) && d.size() % kFrameSize == 0) {
        t.numFrames = static_cast<int>(std::min<std::size_t>(d.size() / kFrameSize, 256));
        t.data.assign(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(t.numFrames) * kFrameSize);
        normalizeTable(t.data);
        return t;
    }
    if (d.empty()) throw std::runtime_error("empty WAV");
    auto cycle = resampleCycle(d.data(), static_cast<int>(d.size()), kFrameSize);
    t.data = spectralMorph({sineFrame(), cycle}, 16);
    t.numFrames = 16;
    return t;
}

} // namespace sg
