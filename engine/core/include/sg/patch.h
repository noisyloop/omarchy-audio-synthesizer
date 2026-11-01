#pragma once

// The authoritative patch (reference src/audio/engine.ts) and preset JSON
// handling with the validation rules of src/ui/preset-io.ts.

#include "sg/dsp.h"
#include "sg/json.h"
#include "sg/messages.h"
#include "sg/params.h"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace sg {

inline constexpr std::size_t kMaxPresetBytes = 128 * 1024;
inline constexpr std::size_t kMaxPresetNameLength = 64;

struct Patch {
    std::string name = "Init";
    std::string category;
    float values[kNumParams];
    std::optional<ModSlotState> slots[kMaxModSlots];
    LfoShape shapes[8];
    int fxOrder[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    Patch();
    /** Reference loadPreset(sanitizePreset(json)). Throws std::runtime_error on structural errors. */
    static Patch fromJson(const Json& preset);
    Json toJson() const;
};

std::string sanitizeName(const std::string& name);

/** Generated tables (cached) plus per-oscillator custom tables. Thread-safe. */
class WavetableBank {
public:
    /** Table for an oscillator given its normalized wavetable param (reference tableForOsc). */
    const WavetableData* tableFor(double wavetableNorm, int osc);
    const WavetableData* named(int index);  // 0..5
    void setCustom(int osc, std::shared_ptr<const WavetableData> table);
    /** Generate all six built-in tables now (startup), so later calls never block audio. */
    void prebuild();

private:
    std::mutex mutex_;
    std::array<std::unique_ptr<WavetableData>, 6> builtIn_;
    std::array<std::shared_ptr<const WavetableData>, 3> custom_;
};

std::unique_ptr<WavetableData> makeTable(const Wavetable& t);

/**
 * Send a whole patch to a processor-like sink in the reference syncAll order:
 * allNotesOff, every param, every mod slot, 8 LFO shapes, FX order, 3 wavetables.
 */
template <class Sink>
void syncPatch(Sink& sink, const Patch& p, WavetableBank& bank) {
    sink.allNotesOff();
    for (int i = 0; i < kNumParams; i++) sink.setParam(i, p.values[i]);
    for (int s = 0; s < kMaxModSlots; s++) sink.setModSlot(s, p.slots[s]);
    for (int l = 0; l < 8; l++) sink.setLfoShape(l, p.shapes[l]);
    sink.setFxOrder(p.fxOrder, 8);
    for (int o = 0; o < 3; o++) {
        static const int wtIdx[3] = {paramIndex("osc1.wavetable"), paramIndex("osc2.wavetable"), paramIndex("osc3.wavetable")};
        sink.setWavetable(o, bank.tableFor(p.values[wtIdx[o]], o));
    }
}

} // namespace sg
