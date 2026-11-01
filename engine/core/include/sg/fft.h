#pragma once

// Port of the reference src/shared/fft.ts. Arrays are float (Float32Array in
// the reference); arithmetic is double, stored back as float, as in JS.

#include <vector>

namespace sg {

void fft(float* re, float* im, int n);
void ifft(float* re, float* im, int n);
std::vector<float> bandlimitCycle(const float* cycle, int n, int maxHarmonic);
std::vector<float> resampleCycle(const float* input, int length, int size);

} // namespace sg
