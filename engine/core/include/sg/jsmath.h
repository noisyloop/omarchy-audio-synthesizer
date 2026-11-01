#pragma once

// JavaScript number semantics the port must reproduce exactly.

#include <cmath>

namespace sg {

/** Math.round: halves round toward +infinity (std::round rounds away from zero). */
inline double jsRound(double x) noexcept { return std::floor(x + 0.5); }

/** x % m for doubles (JS remainder keeps the dividend's sign, like fmod). */
inline double jsMod(double x, double m) noexcept { return std::fmod(x, m); }

/** Denormal / tiny-value flush used throughout the reference effects. */
inline double flush(double x) noexcept { return std::fabs(x) < 1e-20 ? 0.0 : x; }

inline double clampd(double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline constexpr double kPi = 3.141592653589793;

} // namespace sg
