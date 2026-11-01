// sg-render: render a preset and a MIDI file (or a note list) to WAV with no
// audio device, and run the parity suite against the reference goldens.

#include "sg/fft.h"
#include "sg/patch.h"
#include "sg/render.h"
#include "sg/version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path defaultPresetDir(const char* argv0) {
    // <repo>/presets/factory when run from a build tree; installed copy otherwise.
    const char* env = std::getenv("SG_FACTORY_PRESETS");
    if (env) return env;
    for (fs::path p = fs::absolute(argv0).parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path())
        if (fs::exists(p / "presets/factory")) return p / "presets/factory";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / ".local/share/soundgineer/factory";
}

sg::Patch loadPreset(const std::string& arg, const fs::path& dir) {
    if (fs::exists(arg)) return sg::Patch::fromJson(sg::Json::parse(readFile(arg)));
    if (fs::exists(dir))
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.path().extension() != ".json") continue;
            auto j = sg::Json::parse(readFile(e.path()));
            if (j["name"].asString() == arg) return sg::Patch::fromJson(j);
        }
    throw std::runtime_error("preset not found: " + arg);
}

// ---- Standard MIDI File -> note events (format 0/1, tempo map, ticks/quarter)
std::vector<sg::NoteEvent> readMidi(const std::string& path) {
    const std::string d = readFile(path);
    auto u8 = [&](std::size_t o) { return static_cast<unsigned char>(d.at(o)); };
    auto be16 = [&](std::size_t o) { return (u8(o) << 8) | u8(o + 1); };
    auto be32 = [&](std::size_t o) { return (static_cast<std::uint32_t>(be16(o)) << 16) | static_cast<std::uint32_t>(be16(o + 2)); };
    if (d.compare(0, 4, "MThd") != 0) throw std::runtime_error("not a MIDI file");
    const int ntrks = be16(10), division = be16(12);
    if (division & 0x8000) throw std::runtime_error("SMPTE time division not supported");
    struct Raw { std::uint64_t tick; int kind; int note; int vel; std::uint32_t tempo; };
    std::vector<Raw> evs;
    std::size_t off = 8 + be32(4);
    for (int t = 0; t < ntrks && off + 8 <= d.size(); t++) {
        if (d.compare(off, 4, "MTrk") != 0) throw std::runtime_error("bad track chunk");
        const std::size_t end = off + 8 + be32(off + 4);
        std::size_t p = off + 8;
        std::uint64_t tick = 0;
        int status = 0;
        auto vlq = [&]() { std::uint32_t v = 0; unsigned char c; do { c = u8(p++); v = (v << 7) | (c & 0x7F); } while (c & 0x80); return v; };
        while (p < end) {
            tick += vlq();
            int b = u8(p);
            if (b & 0x80) { status = b; p++; }
            if (status == 0xFF) {
                const int type = u8(p++);
                const std::uint32_t len = vlq();
                if (type == 0x51 && len == 3) evs.push_back({tick, 2, 0, 0, (static_cast<std::uint32_t>(u8(p)) << 16) | (u8(p + 1) << 8) | u8(p + 2)});
                p += len;
            } else if (status == 0xF0 || status == 0xF7) {
                p += vlq();
            } else {
                const int hi = status & 0xF0;
                const int a = u8(p++);
                const int b2 = (hi == 0xC0 || hi == 0xD0) ? 0 : u8(p++);
                if (hi == 0x90 && b2 > 0) evs.push_back({tick, 1, a, b2, 0});
                else if (hi == 0x80 || (hi == 0x90 && b2 == 0)) evs.push_back({tick, 0, a, 0, 0});
            }
        }
        off = end;
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
    std::vector<sg::NoteEvent> out;
    std::map<int, std::vector<std::size_t>> open;
    double sec = 0, usPerQ = 500000;
    std::uint64_t last = 0;
    for (const auto& e : evs) {
        sec += static_cast<double>(e.tick - last) * usPerQ / 1e6 / division;
        last = e.tick;
        if (e.kind == 2) usPerQ = e.tempo;
        else if (e.kind == 1) { open[e.note].push_back(out.size()); out.push_back({sec, sec + 3600, e.note, e.vel / 127.0}); }
        else if (!open[e.note].empty()) { out[open[e.note].front()].off = sec; open[e.note].erase(open[e.note].begin()); }
    }
    return out;
}

std::vector<sg::NoteEvent> parseNotes(const std::string& s) {
    // "on:off:note:vel,..." in seconds
    std::vector<sg::NoteEvent> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        sg::NoteEvent n{};
        if (std::sscanf(item.c_str(), "%lf:%lf:%d:%lf", &n.on, &n.off, &n.note, &n.velocity) == 4) out.push_back(n);
    }
    return out;
}

// ---- parity
double residualDb(const sg::Rendered& a, const sg::Rendered& b) {
    const std::size_t n = std::min(a.left.size(), b.left.size());
    double sum = 0;
    for (std::size_t i = 0; i < n; i++) {
        const double dl = static_cast<double>(a.left[i]) - b.left[i];
        const double dr = static_cast<double>(a.right[i]) - b.right[i];
        sum += dl * dl + dr * dr;
    }
    const double rms = std::sqrt(sum / std::max<std::size_t>(1, 2 * n));
    return rms > 0 ? 20 * std::log10(rms) : -400;
}

double spectralDiffDb(const sg::Rendered& a, const sg::Rendered& b) {
    const int N = 2048;
    const std::size_t n = std::min(a.left.size(), b.left.size());
    std::vector<double> ma(N / 2, 0), mb(N / 2, 0);
    std::vector<float> re(N), im(N);
    for (std::size_t start = 0; start + N <= n; start += N) {
        for (int which = 0; which < 2; which++) {
            const auto& src = which ? b.left : a.left;
            for (int i = 0; i < N; i++) { re[i] = src[start + static_cast<std::size_t>(i)]; im[i] = 0; }
            sg::fft(re.data(), im.data(), N);
            auto& m = which ? mb : ma;
            for (int k = 0; k < N / 2; k++) m[static_cast<std::size_t>(k)] += std::hypot(re[k], im[k]);
        }
    }
    double diff = 0;
    int bins = 0;
    for (int k = 1; k < N / 2; k++) {
        const double x = ma[static_cast<std::size_t>(k)], y = mb[static_cast<std::size_t>(k)];
        if (x < 1e-6 && y < 1e-6) continue;
        diff += std::fabs(20 * std::log10((x + 1e-9) / (y + 1e-9)));
        bins++;
    }
    return bins ? diff / bins : 0;
}

int runParity(const fs::path& casesPath, const fs::path& fixtures, const fs::path& presetDir, const std::string& only, double threshold) {
    const sg::Json spec = sg::Json::parse(readFile(casesPath));
    const double sr = spec["sampleRate"].asNumber(44100);
    const auto seed = static_cast<std::uint32_t>(spec["seed"].asNumber(0));
    sg::WavetableBank bank;
    int failures = 0, ran = 0;
    double worst = -400;
    std::printf("%-28s %12s %12s  %s\n", "case", "residual dB", "spectral dB", "result");
    for (const auto& c : spec["cases"].items()) {
        const std::string name = c["name"].asString();
        if (!only.empty() && name != only) continue;
        sg::Patch patch;
        if (c.has("preset")) patch = loadPreset(c["preset"].asString(), presetDir);
        else {
            sg::Json pj = sg::Json::object();
            pj.set("version", 1);
            pj.set("name", name);
            sg::Json params = sg::Json::object();
            for (const auto& [id, v] : c["raw"].members()) {
                const int i = sg::paramIndex(id);
                if (i < 0) throw std::runtime_error("unknown param in case: " + id);
                params.set(id, sg::valueToNorm(sg::kParams[i], v.asNumber()));
            }
            pj.set("params", std::move(params));
            if (c.has("mods")) pj.set("mods", c["mods"]);
            patch = sg::Patch::fromJson(pj);
        }
        std::vector<sg::NoteEvent> notes;
        for (const auto& n : c["notes"].items())
            notes.push_back({n[0].asNumber(), n[1].asNumber(), static_cast<int>(n[2].asNumber()), n[3].asNumber()});
        const auto ours = sg::renderOffline(patch, notes, c["seconds"].asNumber(), sr, seed, bank);
        const auto ref = sg::readWav((fixtures / (name + ".wav")).c_str(), nullptr);
        const double res = residualDb(ours, ref);
        const double spec_ = spectralDiffDb(ours, ref);
        const bool ok = res <= threshold && ours.left.size() == ref.left.size();
        worst = std::max(worst, res);
        std::printf("%-28s %12.1f %12.2f  %s\n", name.c_str(), res, spec_, ok ? "pass" : "FAIL");
        failures += ok ? 0 : 1;
        ran++;
    }
    std::printf("%d cases, %d failed, worst residual %.1f dBFS (threshold %.0f)\n", ran, failures, worst, threshold);
    return failures ? 1 : 0;
}

void usage() {
    std::printf(
        "Usage:\n"
        "  sg-render --preset <name|file.json> (--midi <file.mid> | --notes on:off:note:vel,...)\n"
        "            --out <file.wav> [--seconds S] [--rate 48000] [--seed N]\n"
        "  sg-render --parity <cases.json> <fixtures-dir> [--case NAME] [--threshold -60]\n"
        "Factory presets are looked up in $SG_FACTORY_PRESETS, <repo>/presets/factory,\n"
        "or ~/.local/share/soundgineer/factory.\n");
}

} // namespace

int main(int argc, char** argv) try {
    std::map<std::string, std::string> opt;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--version") { std::printf("sg-render %s\n", sg::versionString()); return 0; }
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (a.rfind("--", 0) == 0 && i + 1 < argc && a != "--parity") opt[a] = argv[++i];
        else if (a == "--parity") opt[a] = "1";
        else pos.push_back(a);
    }
    const fs::path presetDir = defaultPresetDir(argv[0]);
    if (opt.count("--parity")) {
        if (pos.size() < 2) { usage(); return 2; }
        const double thr = opt.count("--threshold") ? std::stod(opt["--threshold"]) : -60;
        return runParity(pos[0], pos[1], presetDir, opt.count("--case") ? opt["--case"] : "", thr);
    }
    if (!opt.count("--preset") || !opt.count("--out")) { usage(); return 2; }
    const sg::Patch patch = loadPreset(opt["--preset"], presetDir);
    std::vector<sg::NoteEvent> notes = opt.count("--midi") ? readMidi(opt["--midi"])
                                     : opt.count("--notes") ? parseNotes(opt["--notes"])
                                     : std::vector<sg::NoteEvent>{{0, 1, 60, 1}};
    double seconds = 0;
    for (auto& n : notes) { n.off = std::min(n.off, n.on + 600); seconds = std::max(seconds, n.off); }
    seconds = opt.count("--seconds") ? std::stod(opt["--seconds"]) : seconds + 2;
    const double sr = opt.count("--rate") ? std::stod(opt["--rate"]) : 48000;
    const auto seed = static_cast<std::uint32_t>(opt.count("--seed") ? std::stoul(opt["--seed"]) : 0);
    sg::WavetableBank bank;
    const auto audio = sg::renderOffline(patch, notes, seconds, sr, seed, bank);
    if (!sg::writeWavFloat(opt["--out"].c_str(), audio, static_cast<int>(sr))) {
        std::fprintf(stderr, "sg-render: cannot write %s\n", opt["--out"].c_str());
        return 1;
    }
    std::printf("wrote %s (%s, %zu notes, %.2f s)\n", opt["--out"].c_str(), patch.name.c_str(), notes.size(), seconds);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "sg-render: %s\n", e.what());
    return 1;
}
