#pragma once

// Gamepad -> Soundgineer mapping. Pure logic: no devices, no sockets, so it
// can be unit tested. sg_pad.cpp feeds it normalized controls and sends the
// protocol messages it emits (docs/protocol.md) to the engine.

#include "sg/json.h"

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgpad {

/** Logical controls. Sticks are -1..1 (up is +1 on the Y axes), triggers 0..1. */
enum class Axis { LX, LY, RX, RY, L2, R2, Count };
enum class Button {
    South, East, West, North, L1, R1, L3, R3, Select, Start, Mode,
    Up, Down, Left, Right, Count
};

const char* axisName(Axis a);
const char* buttonName(Button b);
bool parseAxis(const std::string& s, Axis& out);
bool parseButton(const std::string& s, Button& out);

/**
 * How an axis drives its target.
 *  abs     target follows the axis (for pitchBend / modWheel / params)
 *  rel     the axis pushes the value at `speed` per second; it stays put
 *          when you let go (sticks as endless knobs)
 *  offset  value = base + amount * axis, back to base on release (triggers
 *          as momentary sweeps over whatever the patch has)
 */
struct AxisBinding {
    Axis axis = Axis::LX;
    std::string target;  // param id, or "pitchBend" / "modWheel" / "aftertouch"
    enum class Mode { Abs, Rel, Offset } mode = Mode::Abs;
    double speed = 0.8;  // rel: full range per 1/speed seconds
    double amount = 0.5; // offset
    bool invert = false;
    bool positiveOnly = false; // use only the + half of a stick (e.g. push up for mod wheel)
};

/** A button plays a scale degree or runs an action. */
struct ButtonBinding {
    Button button = Button::South;
    int degree = -1;     // >= 0: scale degree (may exceed the scale; wraps up octaves)
    std::string action;  // octaveUp, octaveDown, transposeUp, transposeDown,
                         // nextPreset, prevPreset, sustain, panic
};

struct Config {
    double deadzone = 0.10;
    int root = 48;                                 // C3
    std::vector<int> scale = {0, 3, 5, 7, 10};     // minor pentatonic
    double velocity = 0.9;
    bool rumble = true;
    bool lightbar = true;
    std::string lightParam = "macro1.value";       // lightbar hue follows this
    std::vector<AxisBinding> axes;
    std::vector<ButtonBinding> buttons;

    static Config defaults();
    /** Overlay a user JSON file on top of the defaults. Throws on bad input. */
    static Config fromJson(const sg::Json& j);
    sg::Json toJson() const;
};

struct Rgb { double r = 0, g = 0, b = 0; };

class Mapper {
public:
    using Send = std::function<void(const sg::Json&)>;

    Mapper(Config cfg, Send send);

    void axis(Axis a, double v);
    void button(Button b, bool down);
    /** Advance time: integrates rel bindings and flushes changed values. */
    void tick(double dt);

    /** Engine replies: `state` (params, patchName), `presetList`, `status`, `param`. */
    void onEngine(const sg::Json& msg);

    /** Current lightbar color: hue from lightParam, brightness from output peak. */
    Rgb light() const;
    /** True once after a note-on, for a short rumble tick. */
    bool takeRumble() { const bool r = rumble_; rumble_ = false; return r; }

    int octave() const { return octave_; }
    int transpose() const { return transpose_; }
    double value(const std::string& param) const;

    const Config& config() const { return cfg_; }

private:
    double shaped(const AxisBinding& b, double raw) const;
    void setTarget(const std::string& target, double v);
    int noteFor(int degree) const;
    void stepPreset(int dir);

    Config cfg_;
    Send send_;
    std::array<double, static_cast<int>(Axis::Count)> axes_{};
    std::unordered_map<std::string, double> base_;    // patch values from the engine
    std::unordered_map<std::string, double> current_; // what we last asked for
    std::unordered_map<std::string, double> pending_; // to flush on tick
    std::unordered_map<std::string, double> rel_;     // unrounded rel knob positions
    std::unordered_map<int, int> held_;               // button -> sounding note
    std::vector<std::string> presets_;
    std::string patchName_;
    int octave_ = 0;
    int transpose_ = 0;
    double peak_ = 0;
    bool rumble_ = false;
};

} // namespace sgpad
