#pragma once

#include <cstdint>

namespace sg {

/**
 * mulberry32, bit-exact with the JavaScript version the parity harness
 * installs as Math.random:
 *
 *   a = a + 0x6D2B79F5 | 0
 *   t = Math.imul(a ^ a >>> 15, 1 | a)
 *   t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t
 *   return ((t ^ t >>> 14) >>> 0) / 4294967296
 *
 * The port calls next() exactly where the reference calls Math.random(), in
 * the same order, so seeded renders match sample for sample.
 * Real-time safe: no allocation, no locks.
 */
class Rng {
public:
    explicit constexpr Rng(std::uint32_t seed = 0) noexcept : state_(seed) {}

    constexpr void seed(std::uint32_t s) noexcept { state_ = s; }

    /** Uniform double in [0, 1), like Math.random(). */
    constexpr double next() noexcept {
        state_ += 0x6D2B79F5u;
        std::uint32_t t = (state_ ^ (state_ >> 15)) * (1u | state_);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }

private:
    std::uint32_t state_;
};

} // namespace sg
