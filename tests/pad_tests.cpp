// sg-pad mapping tests: feed logical controls, check the protocol messages.

#include "pad_map.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

using sgpad::Axis;
using sgpad::Button;

struct Rig {
    std::vector<sg::Json> out;
    sgpad::Mapper m;
    explicit Rig(sgpad::Config c = sgpad::Config::defaults())
        : m(std::move(c), [this](const sg::Json& j) { out.push_back(j); }) {}

    const sg::Json* last(const std::string& type, const std::string& id = "") const {
        for (auto it = out.rbegin(); it != out.rend(); ++it)
            if ((*it)["type"].asString() == type && (id.empty() || (*it)["id"].asString() == id)) return &*it;
        return nullptr;
    }
    void state(const char* json) { m.onEngine(sg::Json::parse(json)); }
};

bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

void notesFollowScaleAndOctave() {
    Rig r;
    r.m.button(Button::South, true);
    const auto* on = r.last("noteOn");
    CHECK(on && (*on)["note"].asNumber() == 48);
    r.m.button(Button::Up, true);  // octave up while held
    r.m.button(Button::South, false);
    const auto* off = r.last("noteOff");
    CHECK(off && (*off)["note"].asNumber() == 48);  // releases the note that sounded
    r.m.button(Button::East, true);
    CHECK(r.last("noteOn") && (*r.last("noteOn"))["note"].asNumber() == 48 + 12 + 3);
    CHECK(r.m.takeRumble());
    CHECK(!r.m.takeRumble());
}

void stickTurnsKnobFromPatchValue() {
    Rig r;
    r.state(R"({"type":"state","patchName":"Init","params":{"macro1.value":0.5,"filter1.cutoff":0.3}})");
    r.m.axis(Axis::LX, 1.0);
    for (int i = 0; i < 25; ++i) r.m.tick(0.01);  // 0.25 s at 0.8/s
    const auto* p = r.last("param", "macro1.value");
    CHECK(p && near((*p)["value"].asNumber(), 0.7));
    r.m.axis(Axis::LX, 0.0);
    const std::size_t n = r.out.size();
    r.m.tick(0.01);
    CHECK(r.out.size() == n);  // released stick: knob stays, nothing sent
    CHECK(near(r.m.value("macro1.value"), 0.7));
}

void slowPushStillMoves() {
    Rig r;
    r.state(R"({"type":"state","params":{"macro1.value":0.5}})");
    r.m.axis(Axis::LX, 0.12);  // just past the deadzone
    for (int i = 0; i < 1000; ++i) r.m.tick(0.008);
    CHECK(r.m.value("macro1.value") > 0.51);
}

void triggerSweepsAndReturns() {
    Rig r;
    r.state(R"({"type":"state","params":{"filter1.cutoff":0.3}})");
    r.m.axis(Axis::R2, 1.0);
    r.m.tick(0.01);
    CHECK(near((*r.last("param", "filter1.cutoff"))["value"].asNumber(), 0.9));
    r.m.axis(Axis::R2, 0.0);
    r.m.tick(0.01);
    CHECK(near((*r.last("param", "filter1.cutoff"))["value"].asNumber(), 0.3));
}

void rightStickBendsAndModulates() {
    Rig r;
    r.m.axis(Axis::RX, -1.0);
    r.m.axis(Axis::RY, -1.0);  // pulling down: mod wheel uses only the up half
    r.m.tick(0.01);
    CHECK(r.last("pitchBend") && near((*r.last("pitchBend"))["value"].asNumber(), -1.0));
    CHECK(r.last("modWheel") && near((*r.last("modWheel"))["value"].asNumber(), 0.0));
    r.m.axis(Axis::RY, 0.55);
    r.m.tick(0.01);
    CHECK(near((*r.last("modWheel"))["value"].asNumber(), 0.5));
}

void presetsCycle() {
    Rig r;
    r.state(R"({"type":"presetList","presets":[{"name":"A"},{"name":"B"},{"name":"C"}]})");
    r.state(R"({"type":"state","patchName":"C","params":{}})");
    r.m.button(Button::Right, true);
    CHECK(r.last("loadPreset") && (*r.last("loadPreset"))["name"].asString() == "A");
    r.m.button(Button::Left, true);
    CHECK((*r.last("loadPreset"))["name"].asString() == "C");
}

void configRoundTrips() {
    const auto j = sgpad::Config::defaults().toJson();
    const auto c = sgpad::Config::fromJson(sg::Json::parse(j.dump()));
    CHECK(c.axes.size() == sgpad::Config::defaults().axes.size());
    CHECK(c.buttons.size() == sgpad::Config::defaults().buttons.size());
    CHECK(c.toJson().dump() == j.dump());
    bool threw = false;
    try { sgpad::Config::fromJson(sg::Json::parse(R"({"axes":[{"axis":"zz","target":"x"}]})")); }
    catch (...) { threw = true; }
    CHECK(threw);
}

void lightFollowsMacroAndPeak() {
    Rig r;
    r.state(R"({"type":"state","params":{"macro1.value":0}})");
    auto c = r.m.light();
    CHECK(c.r > 0.2 && c.g == 0 && c.b == 0);  // red, dim when silent
    r.state(R"({"type":"status","peakL":1,"peakR":0.5})");
    CHECK(r.m.light().r == 1.0);
}

} // namespace

int main() {
    notesFollowScaleAndOctave();
    stickTurnsKnobFromPatchValue();
    slowPushStillMoves();
    triggerSweepsAndReturns();
    rightStickBendsAndModulates();
    presetsCycle();
    configRoundTrips();
    lightFollowsMacroAndPeak();
    if (failures) { std::fprintf(stderr, "%d pad check(s) failed\n", failures); return 1; }
    std::printf("pad tests passed\n");
    return 0;
}
