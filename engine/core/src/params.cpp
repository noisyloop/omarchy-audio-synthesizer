#include "sg/params.h"

#include "sg/jsmath.h"

#include <cmath>
#include <cstring>

namespace sg {

const ParamDef kParams[] = {
#include "params_table.inc"
};
static_assert(sizeof(kParams) / sizeof(kParams[0]) == kNumParams, "regenerate params_table.inc");

const char* const kWavetableNames[kNumWavetables] = {
    "Basic Shapes", "Harmonic Sweep", "PWM", "Vocal", "FM Bell", "Digital", "Custom"};

int paramIndex(std::string_view id) noexcept {
    for (int i = 0; i < kNumParams; ++i)
        if (id == kParams[i].id) return i;
    return -1;
}

double normToValue(const ParamDef& d, double n) noexcept {
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    if (d.numChoices > 0) return jsRound(n * (d.numChoices - 1));
    double v;
    if (d.curve == Curve::Exp) v = d.min * std::pow(d.max / d.min, n);
    else v = d.min + (d.max - d.min) * n;
    if (d.step != 0) v = jsRound(v / d.step) * d.step;
    return v;
}

double valueToNorm(const ParamDef& d, double v) noexcept {
    if (d.numChoices > 0) return d.numChoices > 1 ? v / (d.numChoices - 1) : 0;
    double n;
    if (d.curve == Curve::Exp) n = std::log(v / d.min) / std::log(d.max / d.min);
    else n = (v - d.min) / (d.max - d.min);
    return n < 0 ? 0 : n > 1 ? 1 : n;
}

void defaultValues(float* out) noexcept {
    for (int i = 0; i < kNumParams; ++i) out[i] = static_cast<float>(valueToNorm(kParams[i], kParams[i].def));
}

double divisionToBeats(int divIndex) noexcept {
    // SYNC_DIVISIONS: 1/1 1/2 1/2T 1/4. 1/4 1/4T 1/8. 1/8 1/8T 1/16. 1/16 1/16T 1/32
    static const double base[] = {4, 2, 2, 1, 1, 1, 0.5, 0.5, 0.5, 0.25, 0.25, 0.25, 0.125};
    static const char kind[] = {' ', ' ', 'T', '.', ' ', 'T', '.', ' ', 'T', '.', ' ', 'T', ' '};
    if (divIndex < 0 || divIndex > 12) return 1;  // reference: name ?? '1/4', base ?? 1
    double b = base[divIndex];
    if (kind[divIndex] == '.') b *= 1.5;
    if (kind[divIndex] == 'T') b *= 2.0 / 3.0;
    return b;
}

} // namespace sg
