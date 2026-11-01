#pragma once

// Constants and shapes from the reference src/shared/messages.ts.
// The full message set lands with the protocol in Phase 4; see docs/protocol.md.

#include <array>
#include <cstddef>
#include <string_view>

namespace sg {

struct ModSourceDef {
    std::string_view id;
    std::string_view name;
    bool perVoice;  // per-voice sources differ per playing note
    bool bipolar;   // bipolar -1..1, otherwise unipolar 0..1
};

// Order matters: indices are used on the wire and in presets' matrix slots.
inline constexpr std::array<ModSourceDef, 24> kModSources{{
    {"env1", "Env 1", true, false}, {"env2", "Env 2", true, false},
    {"env3", "Env 3", true, false}, {"env4", "Env 4", true, false},
    {"env5", "Env 5", true, false}, {"env6", "Env 6", true, false},
    {"lfo1", "LFO 1", true, false}, {"lfo2", "LFO 2", true, false},
    {"lfo3", "LFO 3", true, false}, {"lfo4", "LFO 4", true, false},
    {"lfo5", "LFO 5", true, false}, {"lfo6", "LFO 6", true, false},
    {"lfo7", "LFO 7", true, false}, {"lfo8", "LFO 8", true, false},
    {"velocity", "Velocity", true, false},
    {"keytrack", "Key Track", true, true},
    {"random", "Random", true, false},
    {"macro1", "Macro 1", false, false}, {"macro2", "Macro 2", false, false},
    {"macro3", "Macro 3", false, false}, {"macro4", "Macro 4", false, false},
    {"modwheel", "Mod Wheel", false, false},
    {"pitchwheel", "Pitch Whl", false, true},
    {"aftertouch", "Pressure", false, false},
}};

inline constexpr std::size_t kNumModSources = kModSources.size();

// Source index constants, as in the reference voice.ts.
inline constexpr int kSrcEnv0 = 0;
inline constexpr int kSrcLfo0 = 6;
inline constexpr int kSrcVelocity = 14;
inline constexpr int kSrcKeytrack = 15;
inline constexpr int kSrcRandom = 16;
inline constexpr int kSrcMacro0 = 17;
inline constexpr int kSrcModwheel = 21;
inline constexpr int kSrcPitchwheel = 22;
inline constexpr int kSrcAftertouch = 23;

/** Index of a source id, or -1 (the reference throws). */
constexpr int modSourceIndex(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kModSources.size(); ++i)
        if (kModSources[i].id == id) return static_cast<int>(i);
    return -1;
}

inline constexpr int kMaxModSlots = 32;
inline constexpr int kMaxLfoPoints = 64;
inline constexpr int kMaxVoices = 16;
inline constexpr int kMaxUnison = 16;

/** Fixed control-rate block of the reference AudioWorkletProcessor. */
inline constexpr int kBlockSize = 128;

inline constexpr std::array<std::string_view, 8> kFxIds{
    "chorus", "phaser", "flanger", "delay", "reverb", "eq", "comp", "fxdist"};

/** One point of a drawn LFO: x, y in 0..1; power bends the segment after it. */
struct LfoPoint {
    double x;
    double y;
    double power;  // -1..1, 0 = linear
};

/** A drawn LFO shape: up to kMaxLfoPoints points, sorted by x. */
struct LfoShape {
    LfoPoint points[64];
    int count = 0;
};

/** Reference defaultLfoShape(): rising/falling triangle. */
inline LfoShape defaultLfoShape() noexcept {
    LfoShape s;
    s.points[0] = {0, 0, 0};
    s.points[1] = {0.5, 1, 0};
    s.points[2] = {1, 0, 0};
    s.count = 3;
    return s;
}

/** Evaluate a multi-point LFO shape at phase 0..1 (reference evalLfoShape). */
double evalLfoShape(const LfoShape& shape, double phase) noexcept;

struct ModSlotState {
    int source;    // kModSources index
    int dest;      // parameter index
    double depth;  // -1..1 in normalized parameter units
    bool enabled;
};

} // namespace sg
