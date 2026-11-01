#pragma once

// Parameter registry, ported from the reference src/shared/params.ts.
// The table itself is generated from the reference (src/params_table.inc).

#include <string_view>

namespace sg {

enum class Curve { Lin, Exp };

struct ParamDef {
    const char* id;
    const char* name;
    const char* group;
    double min;
    double max;
    double def;      // raw units
    Curve curve;
    double step;     // 0 = continuous
    int numChoices;  // > 0 for enumerated params
    bool moddable;
};

extern const ParamDef kParams[];
inline constexpr int kNumParams = 223;

/** Index of a parameter id, or -1. */
int paramIndex(std::string_view id) noexcept;

/** Normalized 0..1 -> raw value (clamps n). */
double normToValue(const ParamDef& d, double n) noexcept;
/** Raw value -> normalized 0..1. */
double valueToNorm(const ParamDef& d, double v) noexcept;
/** Default normalized values for every parameter (init patch). */
void defaultValues(float* out) noexcept;

/** Beats per whole-note division (SYNC_DIVISIONS index). */
double divisionToBeats(int divIndex) noexcept;

inline constexpr int kNumWavetables = 7;  // incl. "Custom"
extern const char* const kWavetableNames[kNumWavetables];

} // namespace sg
