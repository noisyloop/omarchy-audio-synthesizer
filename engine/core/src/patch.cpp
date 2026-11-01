#include "sg/patch.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sg {

Patch::Patch() {
    defaultValues(values);
    for (auto& s : shapes) s = defaultLfoShape();
}

std::string sanitizeName(const std::string& raw) {
    std::string out;
    for (const char c : raw)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) out += c;
    const auto b = out.find_first_not_of(" \t");
    const auto e = out.find_last_not_of(" \t");
    out = b == std::string::npos ? "" : out.substr(b, e - b + 1);
    if (out.size() > kMaxPresetNameLength) out.resize(kMaxPresetNameLength);
    return out.empty() ? "Imported Patch" : out;
}

static double finiteNum(const Json& v, const char* what) {
    if (!v.isNumber() || !std::isfinite(v.asNumber())) throw std::runtime_error(std::string(what) + " must be a finite number");
    return v.asNumber();
}

static const std::string& shortId(const Json& v, const char* what) {
    if (!v.isString() || v.asString().empty() || v.asString().size() > 64) throw std::runtime_error(std::string(what) + " must be a short string");
    return v.asString();
}

Patch Patch::fromJson(const Json& raw) {
    if (!raw.isObject()) throw std::runtime_error("not a preset object");
    if (raw["version"].asNumber(-1) != 1) throw std::runtime_error("unsupported preset version");
    Patch p;
    p.name = sanitizeName(raw["name"].asString());
    p.category = raw["category"].isString() ? sanitizeName(raw["category"].asString()) : "";

    const Json& params = raw["params"];
    if (!params.isNull()) {
        if (!params.isObject()) throw std::runtime_error("params must be an object");
        if (params.size() > 1024) throw std::runtime_error("too many params");
        for (const auto& [k, v] : params.members()) {
            if (k.empty() || k.size() > 64) throw std::runtime_error("bad param id");
            const double n = clampd(finiteNum(v, "param value"), 0, 1);
            const int i = paramIndex(k);
            if (i >= 0) p.values[i] = static_cast<float>(n);  // unknown ids are skipped
        }
    }

    const Json& mods = raw["mods"];
    if (!mods.isNull()) {
        if (!mods.isArray() || mods.size() > static_cast<std::size_t>(kMaxModSlots)) throw std::runtime_error("bad mods");
        for (std::size_t i = 0; i < mods.size(); i++) {
            const Json& m = mods[i];
            if (!m.isObject()) throw std::runtime_error("mod must be an object");
            const int src = modSourceIndex(shortId(m["source"], "mod source"));
            const int dst = paramIndex(shortId(m["dest"], "mod dest"));
            const double depth = clampd(finiteNum(m["depth"], "mod depth"), -1, 1);
            const bool enabled = !(m["enabled"].isBool() && !m["enabled"].asBool());
            if (src >= 0 && dst >= 0) p.slots[i] = ModSlotState{src, dst, depth, enabled};
        }
    }

    const Json& shapes = raw["lfoShapes"];
    if (!shapes.isNull()) {
        if (!shapes.isArray() || shapes.size() > 8) throw std::runtime_error("bad lfoShapes");
        for (std::size_t l = 0; l < shapes.size(); l++) {
            const Json& pts = shapes[l];
            if (!pts.isArray() || pts.size() > static_cast<std::size_t>(kMaxLfoPoints)) throw std::runtime_error("bad lfo shape");
            if (pts.size() == 0) continue;  // empty -> default
            LfoShape s;
            for (std::size_t i = 0; i < pts.size(); i++) {
                const Json& pt = pts[i];
                if (!pt.isObject()) throw std::runtime_error("bad lfo point");
                s.points[i] = {clampd(finiteNum(pt["x"], "lfo x"), 0, 1), clampd(finiteNum(pt["y"], "lfo y"), 0, 1),
                               clampd(pt.has("power") ? finiteNum(pt["power"], "lfo power") : 0, -1, 1)};
            }
            s.count = static_cast<int>(pts.size());
            std::stable_sort(s.points, s.points + s.count, [](const LfoPoint& a, const LfoPoint& b) { return a.x < b.x; });
            p.shapes[l] = s;
        }
    }

    const Json& fx = raw["fxOrder"];
    if (!fx.isNull()) {
        if (!fx.isArray() || fx.size() > 32) throw std::runtime_error("bad fxOrder");
        int order[32];
        int count = 0;
        for (const auto& f : fx.items()) {
            const std::string& id = shortId(f, "fx id");
            for (int k = 0; k < 8; k++)
                if (kFxIds[static_cast<std::size_t>(k)] == id) { order[count++] = k; break; }
        }
        if (count == 8) std::copy(order, order + 8, p.fxOrder);
    }
    return p;
}

Json Patch::toJson() const {
    Json out = Json::object();
    out.set("name", name);
    out.set("version", 1);
    if (!category.empty()) out.set("category", category);
    Json params = Json::object();
    for (int i = 0; i < kNumParams; i++) params.set(kParams[i].id, static_cast<double>(values[i]));
    out.set("params", std::move(params));
    Json mods = Json::array();
    for (const auto& s : slots) {
        if (!s) continue;
        Json m = Json::object();
        m.set("source", std::string(kModSources[static_cast<std::size_t>(s->source)].id));
        m.set("dest", kParams[s->dest].id);
        m.set("depth", s->depth);
        m.set("enabled", s->enabled);
        mods.push(std::move(m));
    }
    out.set("mods", std::move(mods));
    Json shapesJ = Json::array();
    for (const auto& sh : shapes) {
        Json pts = Json::array();
        for (int i = 0; i < sh.count; i++) {
            Json pt = Json::object();
            pt.set("x", sh.points[i].x);
            pt.set("y", sh.points[i].y);
            pt.set("power", sh.points[i].power);
            pts.push(std::move(pt));
        }
        shapesJ.push(std::move(pts));
    }
    out.set("lfoShapes", std::move(shapesJ));
    Json fx = Json::array();
    for (int f : fxOrder) fx.push(std::string(kFxIds[static_cast<std::size_t>(f)]));
    out.set("fxOrder", std::move(fx));
    return out;
}

std::unique_ptr<WavetableData> makeTable(const Wavetable& t) {
    return std::make_unique<WavetableData>(buildMips(t.data.data(), t.frameSize, t.numFrames), t.frameSize, t.numFrames);
}

const WavetableData* WavetableBank::named(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = builtIn_[static_cast<std::size_t>(index)];
    if (!slot) slot = makeTable(generateWavetable(kWavetableNames[index]));
    return slot.get();
}

const WavetableData* WavetableBank::tableFor(double norm, int osc) {
    const int custom = kNumWavetables - 1;
    const int sel = static_cast<int>(jsRound(norm * (kNumWavetables - 1)));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sel == custom && custom_[static_cast<std::size_t>(osc)]) return custom_[static_cast<std::size_t>(osc)].get();
    }
    int idx = std::min(sel, custom - 1);
    if (idx < 0) idx = 0;
    return named(idx);
}

void WavetableBank::setCustom(int osc, std::shared_ptr<const WavetableData> table) {
    std::lock_guard<std::mutex> lock(mutex_);
    custom_[static_cast<std::size_t>(osc)] = std::move(table);
}

void WavetableBank::prebuild() {
    for (int i = 0; i < 6; i++) named(i);
}

} // namespace sg
