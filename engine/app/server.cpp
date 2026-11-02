#include "server.h"

#include "sg/fft.h"
#include "sg/version.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace sg {

namespace {

constexpr std::size_t kMaxClients = 8;
constexpr std::size_t kMaxLine = 256 * 1024;
constexpr std::size_t kMaxOutBuffer = 4 * 1024 * 1024;

std::string readFile(const fs::path& p, std::size_t limit) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.size() > limit) return {};
    return s;
}

bool writeFileAtomic(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

std::string slug(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "preset" : out;
}

double num(const Json& v, double def = 0) {
    const double d = v.asNumber(def);
    return std::isfinite(d) ? d : def;
}

int paramFrom(const Json& msg) {
    if (msg["id"].isString()) return paramIndex(msg["id"].asString());
    const double i = num(msg["index"], -1);
    return i >= 0 && i < kNumParams ? static_cast<int>(i) : -1;
}

int sourceFrom(const Json& v) {
    if (v.isString()) return modSourceIndex(v.asString());
    const double i = num(v, -1);
    return i >= 0 && i < static_cast<double>(kNumModSources) ? static_cast<int>(i) : -1;
}

int destFrom(const Json& v) {
    if (v.isString()) return paramIndex(v.asString());
    const double i = num(v, -1);
    return i >= 0 && i < kNumParams ? static_cast<int>(i) : -1;
}

double round4(double v) { return std::round(v * 10000) / 10000; }

const char* const kEmbeddedPresets[] = {
#include "factory_presets.inc"
};

} // namespace

const std::vector<Json>& Server::factory() {
    static const std::vector<Json> list = [] {
        std::vector<Json> out;
        for (const char* text : kEmbeddedPresets) {
            try { out.push_back(Json::parse(text)); } catch (...) {}
        }
        return out;
    }();
    return list;
}

Server::Server(Engine& engine, Paths paths, bool dummyAudio)
    : engine_(engine), paths_(std::move(paths)), dummyAudio_(dummyAudio) {
    const std::string fav = readFile(paths_.state / "favorites.json", 64 * 1024);
    if (!fav.empty()) {
        try {
            const Json parsed = Json::parse(fav);
            for (const auto& f : parsed.items())
                if (f.isString()) favorites_.push_back(f.asString());
        } catch (...) {}
    }
}

Server::~Server() {
    for (auto& c : clients_) ::close(c.fd);
    if (listenFd_ >= 0) ::close(listenFd_);
    if (ownSocket_) ::unlink(paths_.socket.c_str());
}

bool Server::listen(int inheritedFd, std::string& error) {
    if (inheritedFd >= 0) {
        listenFd_ = inheritedFd;
        return true;
    }
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) { error = std::strerror(errno); return false; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (paths_.socket.string().size() >= sizeof addr.sun_path) { error = "socket path too long"; return false; }
    std::strcpy(addr.sun_path, paths_.socket.c_str());
    ::unlink(addr.sun_path);
    const mode_t old = ::umask(0177);  // socket file 0600
    const int rc = ::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::umask(old);
    if (rc != 0 || ::listen(listenFd_, 8) != 0) { error = std::strerror(errno); return false; }
    ownSocket_ = true;
    return true;
}

void Server::run(const volatile bool& stop) {
    using clock = std::chrono::steady_clock;
    auto nextTelemetry = clock::now();
    setPower(true, nullptr);
    while (!stop) {
        std::vector<pollfd> fds;
        fds.push_back({listenFd_, POLLIN, 0});
        for (auto& c : clients_) fds.push_back({c.fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
        const int rc = ::poll(fds.data(), fds.size(), 33);
        if (rc < 0 && errno != EINTR) break;
        if (rc > 0) {
            if (fds[0].revents & POLLIN) accept();
            for (std::size_t i = 1; i < fds.size() && i - 1 < clients_.size(); i++) {
                Client& c = clients_[i - 1];
                if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) readClient(c);
                if (c.fd >= 0 && (fds[i].revents & POLLOUT) && !c.out.empty()) {
                    const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
                    if (n > 0) c.out.erase(0, static_cast<std::size_t>(n));
                    else if (n < 0 && errno != EAGAIN) { ::close(c.fd); c.fd = -1; }
                }
            }
            clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const Client& c) { return c.fd < 0; }), clients_.end());
        }
        engine_.collectGarbage();
        for (const auto& ev : engine_.syncAudioEvents()) {
            Json m = Json::object();
            m.set("type", "param");
            m.set("id", kParams[ev.param].id);
            m.set("value", round4(ev.value));
            broadcast(m);
        }
        if (clock::now() >= nextTelemetry) {
            nextTelemetry = clock::now() + std::chrono::milliseconds(33);  // <= 30 Hz
            sendTelemetry();
        }
    }
}

void Server::accept() {
    const int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return;
    if (clients_.size() >= kMaxClients) { ::close(fd); return; }
    Client c;
    c.fd = fd;
    clients_.push_back(std::move(c));
}

void Server::readClient(Client& c) {
    char buf[16384];
    while (true) {
        const ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
        if (n > 0) {
            c.in.append(buf, static_cast<std::size_t>(n));
            std::size_t nl;
            while ((nl = c.in.find('\n')) != std::string::npos) {
                const std::string line = c.in.substr(0, nl);
                c.in.erase(0, nl + 1);
                if (!line.empty()) handle(c, line);
                if (c.fd < 0) return;
            }
            if (c.in.size() > kMaxLine) { ::close(c.fd); c.fd = -1; return; }
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) { ::close(c.fd); c.fd = -1; }
        return;
    }
}

void Server::send(Client& c, const Json& msg) {
    if (c.fd < 0) return;
    if (c.out.size() > kMaxOutBuffer) return;  // slow client: drop
    c.out += msg.dump();
    c.out += '\n';
}

void Server::broadcast(const Json& msg) {
    const std::string line = msg.dump() + "\n";
    for (auto& c : clients_)
        if (c.fd >= 0 && c.out.size() < kMaxOutBuffer) c.out += line;
}

void Server::reply(Client& c, const char* type, const std::string& message) {
    Json m = Json::object();
    m.set("type", type);
    m.set("message", message);
    send(c, m);
}

void Server::sendTelemetry() {
    Processor::Telemetry t;
    if (!engine_.takeTelemetry(t)) return;
    bool wantStatus = false, wantScope = false;
    for (auto& c : clients_) { wantStatus |= c.status; wantScope |= c.scope; }
    if (!wantStatus && !wantScope) return;

    Json status = Json::object();
    status.set("type", "status");
    status.set("voices", t.voices);
    status.set("peakL", round4(t.peakL));
    status.set("peakR", round4(t.peakR));
    Json src = Json::array();
    for (float v : t.sources) src.push(round4(v));
    status.set("sources", std::move(src));
    const std::string statusLine = status.dump() + "\n";

    std::string scopeLine;
    if (wantScope) {
        Json scope = Json::object();
        scope.set("type", "scope");
        Json l = Json::array(), r = Json::array();
        for (int i = 0; i < Processor::kScopeSize; i += 4) { l.push(round4(t.scopeL[i])); r.push(round4(t.scopeR[i])); }
        scope.set("left", std::move(l));
        scope.set("right", std::move(r));
        // 48-bin log spectrum (40 Hz .. 16 kHz), Hann window, 0..1 over -80..0 dB.
        const int N = Processor::kScopeSize;
        std::vector<float> re(N), im(N, 0.0f);
        for (int i = 0; i < N; i++) {
            const double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / (N - 1));
            re[i] = static_cast<float>((t.scopeL[i] + t.scopeR[i]) * 0.5 * w);
        }
        fft(re.data(), im.data(), N);
        Json bins = Json::array();
        const double sr = engine_.sampleRate();
        for (int b = 0; b < 48; b++) {
            const double f0 = 40 * std::pow(400.0, b / 48.0), f1 = 40 * std::pow(400.0, (b + 1) / 48.0);
            const int k0 = std::max(1, static_cast<int>(f0 * N / sr)), k1 = std::max(k0 + 1, static_cast<int>(f1 * N / sr));
            double m = 0;
            for (int k = k0; k < k1 && k < N / 2; k++) m = std::max(m, std::hypot(static_cast<double>(re[k]), static_cast<double>(im[k])) / (N / 4));
            const double db = 20 * std::log10(m + 1e-9);
            bins.push(round4(std::clamp((db + 80) / 80, 0.0, 1.0)));
        }
        scope.set("spectrum", std::move(bins));
        scopeLine = scope.dump() + "\n";
    }
    for (auto& c : clients_) {
        if (c.fd < 0 || c.out.size() > kMaxOutBuffer) continue;
        if (c.status) c.out += statusLine;
        if (c.scope) c.out += scopeLine;
    }
}

Json Server::stateMessage() {
    const Patch& p = engine_.patch();
    Json m = Json::object();
    m.set("type", "state");
    m.set("powered", powered_);
    Json audio = Json::object();
    audio.set("running", engine_.running());
    audio.set("sampleRate", engine_.sampleRate());
    audio.set("backend", dummyAudio_ ? "dummy" : "jack");
    if (!audioError_.empty()) audio.set("error", audioError_);
    m.set("audio", std::move(audio));
    m.set("patchName", p.name);
    m.set("category", p.category);
    const Json pj = p.toJson();
    m.set("params", pj["params"]);
    m.set("mods", pj["mods"]);
    m.set("lfoShapes", pj["lfoShapes"]);
    m.set("fxOrder", pj["fxOrder"]);
    return m;
}

Json Server::presetListMessage() {
    Json list = Json::array();
    auto scan = [&](const fs::path& dir, bool factory) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            try {
                const Json j = Json::parse(readFile(f, kMaxPresetBytes));
                Json item = Json::object();
                const std::string name = sanitizeName(j["name"].asString());
                item.set("name", name);
                item.set("category", j["category"].isString() ? j["category"].asString() : (factory ? "Factory" : "User"));
                item.set("factory", factory);
                item.set("favorite", std::find(favorites_.begin(), favorites_.end(), name) != favorites_.end());
                item.set("path", f.string());
                list.push(std::move(item));
            } catch (...) {}
        }
    };
    for (const auto& j : factory()) {
        Json item = Json::object();
        const std::string name = sanitizeName(j["name"].asString());
        item.set("name", name);
        item.set("category", j["category"].isString() ? j["category"].asString() : "Factory");
        item.set("factory", true);
        item.set("favorite", std::find(favorites_.begin(), favorites_.end(), name) != favorites_.end());
        item.set("path", "factory:" + name);
        list.push(std::move(item));
    }
    scan(paths_.userPresets, false);
    Json m = Json::object();
    m.set("type", "presetList");
    m.set("presets", std::move(list));
    m.set("userDir", paths_.userPresets.string());
    return m;
}

bool Server::readableWav(const std::string& path, std::string& data, std::string& error) {
    std::error_code ec;
    const fs::path p(path);
    std::string ext = p.extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (ext != ".wav") { error = "only .wav files can be imported"; return false; }
    if (!fs::is_regular_file(p, ec)) { error = "not a regular file"; return false; }
    if (fs::file_size(p, ec) > 64u * 1024 * 1024) { error = "file too large (max 64 MB)"; return false; }
    data = readFile(p, 64u * 1024 * 1024);
    if (data.empty()) { error = "cannot read file"; return false; }
    return true;
}

Json Server::fileListMessage(const std::string& kind) {
    // WAV files from the soundgineer data dir (wavetables/ or samples/) and ~/Music.
    Json files = Json::array();
    std::size_t count = 0;
    auto scan = [&](const fs::path& dir, int maxDepth) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && count < 500; it.increment(ec)) {
            if (ec) break;
            if (it.depth() >= maxDepth) it.disable_recursion_pending();
            std::string ext = it->path().extension().string();
            for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ext != ".wav" || !it->is_regular_file(ec)) continue;
            Json f = Json::object();
            f.set("name", it->path().stem().string());
            f.set("path", it->path().string());
            files.push(std::move(f));
            count++;
        }
    };
    scan(paths_.userData / (kind == "sample" ? "samples" : "wavetables"), 3);
    scan(paths_.music, 3);
    Json m = Json::object();
    m.set("type", "fileList");
    m.set("kind", kind);
    m.set("files", std::move(files));
    return m;
}

bool Server::loadPresetByName(const std::string& nameOrPath, std::string& error) {
    auto tryFile = [&](const fs::path& f) -> bool {
        const std::string text = readFile(f, kMaxPresetBytes);
        if (text.empty()) return false;
        try {
            engine_.loadPatch(Patch::fromJson(Json::parse(text)));
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    };
    const std::string key = nameOrPath.rfind("factory:", 0) == 0 ? nameOrPath.substr(8) : nameOrPath;
    for (const auto& j : factory()) {
        if (sanitizeName(j["name"].asString()) != key) continue;
        try {
            engine_.loadPatch(Patch::fromJson(j));
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    // User presets: paths are accepted only inside the user preset directory.
    for (const auto& dir : {paths_.userPresets}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".json") continue;
            if (e.path().string() == nameOrPath) return tryFile(e.path());
            try {
                if (sanitizeName(Json::parse(readFile(e.path(), kMaxPresetBytes))["name"].asString()) == nameOrPath) return tryFile(e.path());
            } catch (...) {}
        }
    }
    if (error.empty()) error = "preset not found";
    return false;
}

void Server::setPower(bool on, Json* errorOut) {
    if (on == powered_ && (on == engine_.running())) return;
    if (on) {
        std::string err;
        if (engine_.start(dummyAudio_, err)) { powered_ = true; audioError_.clear(); }
        else { powered_ = false; audioError_ = err; if (errorOut) *errorOut = Json(err); }
    } else {
        engine_.stop();
        powered_ = false;
    }
}

void Server::saveSession() {
    writeFileAtomic(paths_.state / "session.json", engine_.patch().toJson().dump() + "\n");
}

void Server::restoreSession() {
    const std::string text = readFile(paths_.state / "session.json", kMaxPresetBytes);
    if (text.empty()) return;
    try { engine_.loadPatch(Patch::fromJson(Json::parse(text))); } catch (...) {}
}

void Server::handle(Client& c, const std::string& line) {
    Json msg;
    try { msg = Json::parse(line); } catch (const std::exception& e) { reply(c, "error", e.what()); return; }
    const std::string type = msg["type"].asString();

    if (type == "hello") {
        Json m = Json::object();
        m.set("type", "hello");
        m.set("version", versionString());
        m.set("protocol", kProtocolVersion);
        send(c, m);
    } else if (type == "getState") {
        send(c, stateMessage());
    } else if (type == "subscribe") {
        c.status = msg["status"].asBool(c.status);
        c.scope = msg["scope"].asBool(c.scope);
    } else if (type == "param") {
        const int i = paramFrom(msg);
        if (i < 0) { reply(c, "error", "unknown param"); return; }
        engine_.setParam(i, num(msg["value"]));
    } else if (type == "noteOn") {
        engine_.noteOn(std::clamp(static_cast<int>(num(msg["note"], 60)), 0, 127), std::clamp(num(msg["velocity"], 1), 0.0, 1.0));
    } else if (type == "noteOff") {
        engine_.noteOff(std::clamp(static_cast<int>(num(msg["note"], 60)), 0, 127));
    } else if (type == "sustain") {
        engine_.sustain(msg["down"].asBool());
    } else if (type == "pitchBend") {
        engine_.pitchBend(std::clamp(num(msg["value"]), -1.0, 1.0));
    } else if (type == "modWheel") {
        engine_.modWheel(std::clamp(num(msg["value"]), 0.0, 1.0));
    } else if (type == "aftertouch") {
        engine_.aftertouch(std::clamp(num(msg["value"]), 0.0, 1.0));
    } else if (type == "allNotesOff") {
        engine_.allNotesOff();
    } else if (type == "mod") {
        const int slot = static_cast<int>(num(msg["slot"], -1));
        if (slot < 0 || slot >= kMaxModSlots) { reply(c, "error", "bad slot"); return; }
        const Json& st = msg["state"];
        if (st.isNull()) engine_.setModSlot(slot, std::nullopt);
        else {
            const int src = sourceFrom(st["source"]), dst = destFrom(st["dest"]);
            if (src < 0 || dst < 0) { reply(c, "error", "bad mod source or dest"); return; }
            engine_.setModSlot(slot, ModSlotState{src, dst, std::clamp(num(st["depth"]), -1.0, 1.0), !(st["enabled"].isBool() && !st["enabled"].asBool())});
        }
    } else if (type == "lfoShape") {
        const int lfo = static_cast<int>(num(msg["lfo"], -1));
        const Json& pts = msg["points"];
        if (lfo < 0 || lfo >= 8 || !pts.isArray() || pts.size() > static_cast<std::size_t>(kMaxLfoPoints)) { reply(c, "error", "bad lfoShape"); return; }
        LfoShape s;
        for (std::size_t i = 0; i < pts.size(); i++)
            s.points[i] = {std::clamp(num(pts[i]["x"]), 0.0, 1.0), std::clamp(num(pts[i]["y"]), 0.0, 1.0), std::clamp(num(pts[i]["power"]), -1.0, 1.0)};
        s.count = static_cast<int>(pts.size());
        std::stable_sort(s.points, s.points + s.count, [](const LfoPoint& a, const LfoPoint& b) { return a.x < b.x; });
        engine_.setLfoShape(lfo, s);
    } else if (type == "fxOrder") {
        int order[8];
        const Json& o = msg["order"];
        if (!o.isArray() || o.size() != 8) { reply(c, "error", "fxOrder needs 8 entries"); return; }
        for (int i = 0; i < 8; i++) {
            const Json& v = o[static_cast<std::size_t>(i)];
            int k = -1;
            if (v.isString()) { for (int f = 0; f < 8; f++) if (kFxIds[static_cast<std::size_t>(f)] == v.asString()) k = f; }
            else k = static_cast<int>(num(v, -1));
            if (k < 0 || k > 7) { reply(c, "error", "bad fx id"); return; }
            order[i] = k;
        }
        engine_.setFxOrder(order, 8);
    } else if (type == "power") {
        Json err;
        setPower(msg["on"].asBool(!powered_), &err);
        broadcast(stateMessage());
    } else if (type == "setWavetable") {
        const int osc = static_cast<int>(num(msg["osc"], -1));
        if (osc < 0 || osc > 2) { reply(c, "error", "bad osc"); return; }
        if (msg["name"].isString()) {
            for (int i = 0; i < kNumWavetables - 1; i++)
                if (msg["name"].asString() == kWavetableNames[i]) {
                    char id[32];
                    std::snprintf(id, sizeof id, "osc%d.wavetable", osc + 1);
                    engine_.setParam(paramIndex(id), static_cast<double>(i) / (kNumWavetables - 1));
                    return;
                }
            reply(c, "error", "unknown wavetable");
            return;
        }
        try {
            std::string data, err;
            if (!readableWav(msg["path"].asString(), data, err)) throw std::runtime_error(err);
            const auto wav = decodeWav(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            std::shared_ptr<const WavetableData> table(makeTable(wavToWavetable(fs::path(msg["path"].asString()).stem().string(), wav)));
            engine_.bank().setCustom(osc, table);
            char id[32];
            std::snprintf(id, sizeof id, "osc%d.wavetable", osc + 1);
            engine_.setParam(paramIndex(id), 1.0);  // Custom slot, re-sends the table
            broadcast(stateMessage());
        } catch (const std::exception& e) { reply(c, "error", e.what()); }
    } else if (type == "setSample") {
        if (msg["path"].isNull()) { engine_.setSample(nullptr); return; }
        try {
            std::string data, err;
            if (!readableWav(msg["path"].asString(), data, err)) throw std::runtime_error(err);
            const auto wav = decodeWav(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            if (wav.channelData.empty() || wav.sampleRate <= 0) throw std::runtime_error("empty WAV");
            auto s = std::make_unique<Sample>();
            s->data = wav.channelData;
            s->sampleRate = wav.sampleRate;
            engine_.setSample(std::move(s));
        } catch (const std::exception& e) { reply(c, "error", e.what()); }
    } else if (type == "listFiles") {
        send(c, fileListMessage(msg["kind"].asString() == "sample" ? "sample" : "wavetable"));
    } else if (type == "listPresets") {
        send(c, presetListMessage());
    } else if (type == "loadPreset") {
        std::string err;
        const std::string key = msg["path"].isString() ? msg["path"].asString() : msg["name"].asString();
        if (!loadPresetByName(key, err)) { reply(c, "error", err); return; }
        saveSession();
        broadcast(stateMessage());
    } else if (type == "savePreset") {
        Patch p = engine_.patch();
        p.name = sanitizeName(msg["name"].asString());
        p.category = msg["category"].isString() ? sanitizeName(msg["category"].asString()) : "User";
        const fs::path file = paths_.userPresets / (slug(p.name) + ".json");
        if (!writeFileAtomic(file, p.toJson().dump() + "\n")) { reply(c, "error", "could not write preset"); return; }
        engine_.patch().name = p.name;
        engine_.patch().category = p.category;
        Json m = Json::object();
        m.set("type", "saved");
        m.set("path", file.string());
        send(c, m);
        broadcast(presetListMessage());
    } else if (type == "setFavorite") {
        const std::string name = sanitizeName(msg["name"].asString());
        favorites_.erase(std::remove(favorites_.begin(), favorites_.end(), name), favorites_.end());
        if (msg["on"].asBool(true)) favorites_.push_back(name);
        Json arr = Json::array();
        for (const auto& f : favorites_) arr.push(f);
        writeFileAtomic(paths_.state / "favorites.json", arr.dump() + "\n");
        broadcast(presetListMessage());
    } else {
        reply(c, "error", "unknown message type: " + type.substr(0, 32));
    }
}

} // namespace sg
