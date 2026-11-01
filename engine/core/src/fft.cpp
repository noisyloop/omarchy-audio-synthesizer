#include "sg/fft.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>

namespace sg {

void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = (-2 * kPi) / len;
        const double wr = std::cos(ang);
        const double wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cwr = 1, cwi = 0;
            const int half = len >> 1;
            for (int j = 0; j < half; j++) {
                const double ur = re[i + j];
                const double ui = im[i + j];
                const double xr = re[i + j + half], xi = im[i + j + half];
                const double vr = xr * cwr - xi * cwi;
                const double vi = xr * cwi + xi * cwr;
                re[i + j] = static_cast<float>(ur + vr);
                im[i + j] = static_cast<float>(ui + vi);
                re[i + j + half] = static_cast<float>(ur - vr);
                im[i + j + half] = static_cast<float>(ui - vi);
                const double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

void ifft(float* re, float* im, int n) {
    for (int i = 0; i < n; i++) im[i] = -im[i];
    fft(re, im, n);
    for (int i = 0; i < n; i++) {
        re[i] = static_cast<float>(static_cast<double>(re[i]) / n);
        im[i] = static_cast<float>(-static_cast<double>(im[i]) / n);
    }
}

std::vector<float> bandlimitCycle(const float* cycle, int n, int maxHarmonic) {
    std::vector<float> re(cycle, cycle + n), im(n, 0.0f);
    fft(re.data(), im.data(), n);
    re[0] = 0;
    im[0] = 0;
    const int half = n >> 1;
    for (int k = 1; k <= half; k++) {
        if (k > maxHarmonic) {
            re[k] = 0; im[k] = 0;
            if (k != half) { re[n - k] = 0; im[n - k] = 0; }
        }
    }
    ifft(re.data(), im.data(), n);
    return re;
}

std::vector<float> resampleCycle(const float* input, int length, int size) {
    int n = 1;
    while (n < length) n <<= 1;
    std::vector<float> src(n);
    for (int i = 0; i < n; i++) {
        const double pos = (static_cast<double>(i) / n) * length;
        const int i0 = static_cast<int>(std::fmod(std::floor(pos), length));
        const int i1 = (i0 + 1) % length;
        const double a = input[i0], b = input[i1];
        src[i] = static_cast<float>(a + (b - a) * (pos - i0));
    }
    std::vector<float> re(src), im(n, 0.0f);
    fft(re.data(), im.data(), n);
    std::vector<float> outRe(size, 0.0f), outIm(size, 0.0f);
    const int bins = std::min(n >> 1, size >> 1);
    const double scale = static_cast<double>(size) / n;
    for (int k = 1; k < bins; k++) {
        outRe[k] = static_cast<float>(re[k] * scale);
        outIm[k] = static_cast<float>(im[k] * scale);
        outRe[size - k] = static_cast<float>(re[n - k] * scale);
        outIm[size - k] = static_cast<float>(im[n - k] * scale);
    }
    ifft(outRe.data(), outIm.data(), size);
    return outRe;
}

} // namespace sg
