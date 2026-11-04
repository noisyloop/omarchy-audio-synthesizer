#include "pad_map.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgpad {

namespace {

constexpr const char* kAxisNames[] = {"lx", "ly", "rx", "ry", "l2", "r2"};
constexpr const char* kButtonNames[] = {
    "south", "east", "west", "north", "l1", "r1", "l3", "r3", "select", "start", "mode",
    "up", "down", "left", "right"};

constexpr const char* kActions[] = {
    "octaveUp", "octaveDown", "transposeUp", "transposeDown",
    "nextPreset", "prevPreset", "sustain", "panic"};

bool isTrigger(Axis a) { return a == Axis::L2 || a == Axis::R2; }
bool isEngineTarget(const std::string& t) { return t == "pitchBend" || t == "modWheel" || t == "aftertouch"; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double round4(double v) { return std::round(v * 10000.0) / 10000.0; }

const char* modeName(AxisBinding::Mode m) {
    switch (m) {
    case AxisBinding::Mode::Rel: return "rel";
    case AxisBinding::Mode::Offset: return "offset";
    default: return "abs";
    }
}

sg::Json msg(const char* type) {
    sg::Json m = sg::Json::object();
    m.set("type", type);
    return m;
}

} // namespace

const char* axisName(Axis a) { return kAxisNames[static_cast<int>(a)]; }
const char* buttonName(Button b) { return kButtonNames[static_cast<int>(b)]; }

bool parseAxis(const std::string& s, Axis& out) {
    for (int i = 0; i < static_cast<int>(Axis::Count); ++i)
        if (s == kAxisNames[i]) { out = static_cast<Axis>(i); return true; }
    return false;
}

bool parseButton(const std::string& s, Button& out) {
    for (int i = 0; i < static_cast<int>(Button::Count); ++i)
        if (s == kButtonNames[i]) { out = static_cast<Button>(i); return true; }
    return false;
}

// ---------------------------------------------------------------- config

Config Config::defaults() {
    Config c;
    using M = AxisBinding::Mode;
    auto ax = [](Axis a, const char* t, M m, double speedOrAmount = 0, bool posOnly = false) {
        AxisBinding b;
        b.axis = a;
        b.target = t;
        b.mode = m;
        if (m == M::Rel) b.speed = speedOrAmount;
        if (m == M::Offset) b.amount = speedOrAmount;
        b.positiveOnly = posOnly;
        return b;
    };
    c.axes = {
        ax(Axis::LX, "macro1.value", M::Rel, 0.8),     // left stick: two endless knobs
        ax(Axis::LY, "macro2.value", M::Rel, 0.8),
        ax(Axis::RX, "pitchBend", M::Abs),             // right stick: bend + wobble
        ax(Axis::RY, "modWheel", M::Abs, 0, true),
        ax(Axis::R2, "filter1.cutoff", M::Offset, 0.6), // triggers: momentary sweeps
        ax(Axis::L2, "dist.drive", M::Offset, 0.7),
    };
    auto note = [](Button b, int d) { ButtonBinding x; x.button = b; x.degree = d; return x; };
    auto act = [](Button b, const char* a) { ButtonBinding x; x.button = b; x.action = a; return x; };
    c.buttons = {
        note(Button::South, 0), note(Button::East, 1), note(Button::West, 2),
        note(Button::North, 3), note(Button::R1, 4),
        act(Button::L1, "sustain"),
        act(Button::Up, "octaveUp"), act(Button::Down, "octaveDown"),
        act(Button::Right, "nextPreset"), act(Button::Left, "prevPreset"),
        act(Button::R3, "transposeUp"), act(Button::L3, "transposeDown"),
        act(Button::Start, "panic"),
    };
    return c;
}

Config Config::fromJson(const sg::Json& j) {
    Config c = defaults();
    if (!j.isObject()) throw std::runtime_error("pad config must be a JSON object");
    if (j.has("deadzone")) c.deadzone = std::clamp(j["deadzone"].asNumber(c.deadzone), 0.0, 0.9);
    if (j.has("root")) c.root = std::clamp(static_cast<int>(j["root"].asNumber(c.root)), 0, 127);
    if (j.has("velocity")) c.velocity = clamp01(j["velocity"].asNumber(c.velocity));
    if (j.has("rumble")) c.rumble = j["rumble"].asBool(c.rumble);
    if (j.has("lightbar")) c.lightbar = j["lightbar"].asBool(c.lightbar);
    if (j.has("lightParam")) c.lightParam = j["lightParam"].asString();
    if (j.has("scale")) {
        c.scale.clear();
        for (const auto& s : j["scale"].items()) c.scale.push_back(static_cast<int>(s.asNumber()));
        if (c.scale.empty()) throw std::runtime_error("scale needs at least one step");
    }
    if (j.has("axes")) {
        c.axes.clear();
        for (const auto& a : j["axes"].items()) {
            AxisBinding b;
            if (!parseAxis(a["axis"].asString(), b.axis))
                throw std::runtime_error("unknown axis: " + a["axis"].asString());
            b.target = a["target"].asString();
            if (b.target.empty()) throw std::runtime_error(std::string("axis ") + axisName(b.axis) + " needs a target");
            const std::string mode = a.has("mode") ? a["mode"].asString() : "abs";
            if (mode == "abs") b.mode = AxisBinding::Mode::Abs;
            else if (mode == "rel") b.mode = AxisBinding::Mode::Rel;
            else if (mode == "offset") b.mode = AxisBinding::Mode::Offset;
            else throw std::runtime_error("unknown mode: " + mode);
            b.speed = a["speed"].asNumber(b.speed);
            b.amount = a["amount"].asNumber(b.amount);
            b.invert = a["invert"].asBool(false);
            b.positiveOnly = a["positiveOnly"].asBool(false);
            c.axes.push_back(b);
        }
    }
    if (j.has("buttons")) {
        c.buttons.clear();
        for (const auto& x : j["buttons"].items()) {
            ButtonBinding b;
            if (!parseButton(x["button"].asString(), b.button))
                throw std::runtime_error("unknown button: " + x["button"].asString());
            if (x.has("degree")) b.degree = std::max(0, static_cast<int>(x["degree"].asNumber()));
            else if (x.has("action")) {
                b.action = x["action"].asString();
                if (std::find_if(std::begin(kActions), std::end(kActions),
                        [&](const char* a) { return b.action == a; }) == std::end(kActions))
                    throw std::runtime_error("unknown action: " + b.action);
            } else throw std::runtime_error(std::string("button ") + buttonName(b.button) + " needs a degree or an action");
            c.buttons.push_back(b);
        }
    }
    return c;
}

sg::Json Config::toJson() const {
    sg::Json j = sg::Json::object();
    j.set("deadzone", deadzone);
    j.set("root", root);
    sg::Json sc = sg::Json::array();
    for (int s : scale) sc.push(s);
    j.set("scale", sc);
    j.set("velocity", velocity);
    j.set("rumble", rumble);
    j.set("lightbar", lightbar);
    j.set("lightParam", lightParam);
    sg::Json ax = sg::Json::array();
    for (const auto& b : axes) {
        sg::Json o = sg::Json::object();
        o.set("axis", axisName(b.axis));
        o.set("target", b.target);
        o.set("mode", modeName(b.mode));
        if (b.mode == AxisBinding::Mode::Rel) o.set("speed", b.speed);
        if (b.mode == AxisBinding::Mode::Offset) o.set("amount", b.amount);
        if (b.invert) o.set("invert", true);
        if (b.positiveOnly) o.set("positiveOnly", true);
        ax.push(o);
    }
    j.set("axes", ax);
    sg::Json bt = sg::Json::array();
    for (const auto& b : buttons) {
        sg::Json o = sg::Json::object();
        o.set("button", buttonName(b.button));
        if (b.degree >= 0) o.set("degree", b.degree);
        else o.set("action", b.action);
        bt.push(o);
    }
    j.set("buttons", bt);
    return j;
}

// ---------------------------------------------------------------- mapper

Mapper::Mapper(Config cfg, Send send) : cfg_(std::move(cfg)), send_(std::move(send)) {}

double Mapper::shaped(const AxisBinding& b, double raw) const {
    double v = raw;
    const double dz = cfg_.deadzone;
    const double mag = std::fabs(v);
    v = mag <= dz ? 0.0 : std::copysign((mag - dz) / (1.0 - dz), v);
    if (b.invert) v = isTrigger(b.axis) ? 1.0 - v : -v;
    if (b.positiveOnly) v = std::max(0.0, v);
    return std::clamp(v, -1.0, 1.0);
}

double Mapper::value(const std::string& param) const {
    if (auto it = current_.find(param); it != current_.end()) return it->second;
    if (auto it = base_.find(param); it != base_.end()) return it->second;
    return 0.0;
}

void Mapper::setTarget(const std::string& target, double v) { pending_[target] = v; }

void Mapper::axis(Axis a, double v) {
    axes_[static_cast<int>(a)] = v;
    for (const auto& b : cfg_.axes) {
        if (b.axis != a || b.mode == AxisBinding::Mode::Rel) continue;
        const double s = shaped(b, v);
        if (b.mode == AxisBinding::Mode::Offset) {
            const auto it = base_.find(b.target);
            const double base = it != base_.end() ? it->second : 0.0;
            setTarget(b.target, clamp01(base + b.amount * s));
        } else if (b.target == "pitchBend") {
            setTarget(b.target, s);
        } else {
            // Triggers and half-sticks already span 0..1; a full stick maps -1..1 to 0..1.
            const bool unipolar = isTrigger(a) || b.positiveOnly;
            setTarget(b.target, clamp01(unipolar ? std::fabs(s) : (s + 1.0) * 0.5));
        }
    }
}

int Mapper::noteFor(int degree) const {
    const int n = static_cast<int>(cfg_.scale.size());
    const int note = cfg_.root + 12 * (octave_ + degree / n) + transpose_ + cfg_.scale[degree % n];
    return std::clamp(note, 0, 127);
}

void Mapper::stepPreset(int dir) {
    if (presets_.empty()) { send_(msg("listPresets")); return; }
    const int n = static_cast<int>(presets_.size());
    const auto it = std::find(presets_.begin(), presets_.end(), patchName_);
    int i = it == presets_.end() ? (dir > 0 ? -1 : 0) : static_cast<int>(it - presets_.begin());
    i = ((i + dir) % n + n) % n;
    patchName_ = presets_[i];
    sg::Json m = msg("loadPreset");
    m.set("name", patchName_);
    send_(m);
}

void Mapper::button(Button btn, bool down) {
    for (const auto& b : cfg_.buttons) {
        if (b.button != btn) continue;
        if (b.degree >= 0) {
            const int key = static_cast<int>(btn);
            if (down) {
                if (held_.count(key)) continue;
                const int note = noteFor(b.degree);
                held_[key] = note;
                sg::Json m = msg("noteOn");
                m.set("note", note);
                m.set("velocity", cfg_.velocity);
                send_(m);
                rumble_ = cfg_.rumble;
            } else if (auto it = held_.find(key); it != held_.end()) {
                sg::Json m = msg("noteOff");
                m.set("note", it->second);  // the note that sounded, even if the octave moved since
                send_(m);
                held_.erase(it);
            }
            continue;
        }
        const std::string& a = b.action;
        if (a == "sustain") {
            sg::Json m = msg("sustain");
            m.set("down", down);
            send_(m);
            continue;
        }
        if (!down) continue;
        if (a == "octaveUp") octave_ = std::min(3, octave_ + 1);
        else if (a == "octaveDown") octave_ = std::max(-3, octave_ - 1);
        else if (a == "transposeUp") transpose_ = std::min(11, transpose_ + 1);
        else if (a == "transposeDown") transpose_ = std::max(-11, transpose_ - 1);
        else if (a == "nextPreset") stepPreset(+1);
        else if (a == "prevPreset") stepPreset(-1);
        else if (a == "panic") { held_.clear(); send_(msg("allNotesOff")); }
    }
}

void Mapper::tick(double dt) {
    for (const auto& b : cfg_.axes) {
        if (b.mode != AxisBinding::Mode::Rel) continue;
        const double s = shaped(b, axes_[static_cast<int>(b.axis)]);
        if (s == 0.0) continue;
        // Integrate unrounded, so slow pushes are not lost to the 4-decimal rounding.
        auto [it, fresh] = rel_.try_emplace(b.target, 0.0);
        if (fresh) it->second = value(b.target);
        it->second = clamp01(it->second + s * b.speed * dt);
        setTarget(b.target, it->second);
    }
    for (const auto& [target, raw] : pending_) {
        const double v = round4(raw);
        const auto it = current_.find(target);
        if (it != current_.end() && std::fabs(it->second - v) < 1e-4) continue;
        current_[target] = v;
        sg::Json m;
        if (isEngineTarget(target)) {
            m = msg(target.c_str());
            m.set("value", v);
        } else {
            m = msg("param");
            m.set("id", target);
            m.set("value", v);
        }
        send_(m);
    }
    pending_.clear();
}

void Mapper::onEngine(const sg::Json& m) {
    const std::string& type = m["type"].asString();
    if (type == "state") {
        if (m["patchName"].isString()) patchName_ = m["patchName"].asString();
        for (const auto& [id, v] : m["params"].members()) {
            if (!v.isNumber()) continue;
            base_[id] = v.asNumber();
            current_.erase(id);  // a new patch: knobs continue from its values
            rel_.erase(id);
        }
    } else if (type == "presetList") {
        presets_.clear();
        for (const auto& p : m["presets"].items())
            if (p["name"].isString()) presets_.push_back(p["name"].asString());
    } else if (type == "status") {
        peak_ = std::max(m["peakL"].asNumber(), m["peakR"].asNumber());
    } else if (type == "param" && m["id"].isString()) {
        base_[m["id"].asString()] = m["value"].asNumber();
        current_[m["id"].asString()] = m["value"].asNumber();
        rel_.erase(m["id"].asString());
    }
}

Rgb Mapper::light() const {
    const double h = clamp01(value(cfg_.lightParam)) * 300.0;  // red through magenta
    const double v = 0.25 + 0.75 * std::min(1.0, peak_ * 1.4);
    const double c = v, x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const int seg = static_cast<int>(h / 60.0) % 6;
    switch (seg) {
    case 0: return {c, x, 0};
    case 1: return {x, c, 0};
    case 2: return {0, c, x};
    case 3: return {0, x, c};
    case 4: return {x, 0, c};
    default: return {c, 0, x};
    }
}

} // namespace sgpad
