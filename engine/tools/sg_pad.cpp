// sg-pad: play and tweak Soundgineer from a game controller.
//
// Reads a gamepad through Linux evdev (DualSense, DualShock 4, Xbox, 8BitDo,
// anything the kernel exposes as a joystick) and drives the running engine
// over its Unix socket, the same protocol sg-ctl and the panel use. No MIDI
// port, no extra libraries, no root: systemd-logind already gives the seat
// user access to joystick event nodes.
//
// Optional extras on Linux: a short rumble on each note, and the DualSense /
// DS4 lightbar following a macro and the output level (needs write access to
// the LED in sysfs; see packaging/udev/70-soundgineer-pad.rules).

#include "pad_map.h"

#include "sg/json.h"
#include "sg/version.h"

#include <linux/input.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using sgpad::Axis;
using sgpad::Button;

namespace {

volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

constexpr std::size_t kBits = sizeof(unsigned long) * 8;
bool testBit(const std::vector<unsigned long>& bits, unsigned n) {
    return (bits[n / kBits] >> (n % kBits)) & 1UL;
}

std::vector<unsigned long> capBits(int fd, int ev, unsigned max) {
    std::vector<unsigned long> bits(max / kBits + 1, 0);
    if (::ioctl(fd, EVIOCGBIT(ev, bits.size() * sizeof(unsigned long)), bits.data()) < 0)
        std::fill(bits.begin(), bits.end(), 0);
    return bits;
}

std::string deviceName(int fd) {
    char name[256] = "?";
    ::ioctl(fd, EVIOCGNAME(sizeof name), name);
    return name;
}

bool isGamepad(int fd) {
    const auto keys = capBits(fd, EV_KEY, KEY_MAX);
    const auto abs = capBits(fd, EV_ABS, ABS_MAX);
    // Motion-sensor and touchpad nodes of the same controller have no face buttons.
    return testBit(keys, BTN_SOUTH) && testBit(abs, ABS_X);
}

std::vector<std::string> eventNodes() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/dev/input", ec))
        if (e.path().filename().string().rfind("event", 0) == 0) out.push_back(e.path().string());
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return std::atoi(a.c_str() + 16) < std::atoi(b.c_str() + 16);
    });
    return out;
}

// ---------------------------------------------------------------- gamepad

struct AxisRange { int min = 0, max = 255; bool present = false; };

class Pad {
public:
    ~Pad() { close(); }

    bool open(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        canWrite_ = fd_ >= 0;
        if (fd_ < 0) fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) return false;
        if (!isGamepad(fd_)) { close(); return false; }
        path_ = path;
        name_ = deviceName(fd_);
        for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE}) {
            input_absinfo info{};
            if (::ioctl(fd_, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum)
                ranges_[code] = {info.minimum, info.maximum, true};
        }
        findLed();
        setupRumble();
        return true;
    }

    bool openFirst() {
        for (const auto& p : eventNodes()) if (open(p)) return true;
        return false;
    }

    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        rumbleId_ = -1;
        led_.clear();
    }

    int fd() const { return fd_; }
    const std::string& name() const { return name_; }
    const std::string& path() const { return path_; }

    /** Drain pending events into the mapper. Returns false if the device went away. */
    bool read(sgpad::Mapper& m) {
        input_event ev[64];
        for (;;) {
            const ssize_t n = ::read(fd_, ev, sizeof ev);
            if (n < 0) return errno == EAGAIN || errno == EINTR;
            if (n == 0) return false;
            for (ssize_t i = 0; i < n / static_cast<ssize_t>(sizeof(input_event)); ++i) handle(ev[i], m);
        }
    }

    void rumble() {
        if (rumbleId_ < 0) return;
        input_event play{};
        play.type = EV_FF;
        play.code = static_cast<unsigned short>(rumbleId_);
        play.value = 1;
        [[maybe_unused]] auto r = ::write(fd_, &play, sizeof play);
    }

    void light(const sgpad::Rgb& c) {
        if (led_.empty()) return;
        const int r = static_cast<int>(c.r * 255), g = static_cast<int>(c.g * 255), b = static_cast<int>(c.b * 255);
        if (r == lastRgb_[0] && g == lastRgb_[1] && b == lastRgb_[2]) return;
        std::ofstream f(led_ + "/multi_intensity");
        if (!f) { led_.clear(); return; }
        f << r << ' ' << g << ' ' << b << '\n';
        lastRgb_[0] = r; lastRgb_[1] = g; lastRgb_[2] = b;
    }

    bool hasLight() const { return !led_.empty(); }
    bool hasRumble() const { return rumbleId_ >= 0; }
    const std::string& ledHint() const { return ledHint_; }

private:
    double norm(int code, int v, bool trigger) const {
        const auto it = ranges_.find(code);
        const AxisRange r = it != ranges_.end() ? it->second : AxisRange{};
        const double t = (static_cast<double>(v) - r.min) / (r.max - r.min);
        return trigger ? std::clamp(t, 0.0, 1.0) : std::clamp(t * 2.0 - 1.0, -1.0, 1.0);
    }

    void hat(sgpad::Mapper& m, Button neg, Button pos, int v, int& last) {
        if (last < 0) m.button(neg, false);
        if (last > 0) m.button(pos, false);
        if (v < 0) m.button(neg, true);
        if (v > 0) m.button(pos, true);
        last = v;
    }

    void handle(const input_event& e, sgpad::Mapper& m) {
        if (e.type == EV_ABS) {
            switch (e.code) {
            case ABS_X: m.axis(Axis::LX, norm(e.code, e.value, false)); break;
            case ABS_Y: m.axis(Axis::LY, -norm(e.code, e.value, false)); break;  // up is +1
            case ABS_RX: m.axis(Axis::RX, norm(e.code, e.value, false)); break;
            case ABS_RY: m.axis(Axis::RY, -norm(e.code, e.value, false)); break;
            case ABS_Z: case ABS_BRAKE: m.axis(Axis::L2, norm(e.code, e.value, true)); break;
            case ABS_RZ: case ABS_GAS: m.axis(Axis::R2, norm(e.code, e.value, true)); break;
            case ABS_HAT0X: hat(m, Button::Left, Button::Right, e.value, hatX_); break;
            case ABS_HAT0Y: hat(m, Button::Up, Button::Down, e.value, hatY_); break;
            default: break;
            }
        } else if (e.type == EV_KEY && e.value != 2) {  // ignore autorepeat
            const bool down = e.value == 1;
            std::optional<Button> b;
            switch (e.code) {
            case BTN_SOUTH: b = Button::South; break;
            case BTN_EAST: b = Button::East; break;
            case BTN_WEST: b = Button::West; break;
            case BTN_NORTH: b = Button::North; break;
            case BTN_TL: b = Button::L1; break;
            case BTN_TR: b = Button::R1; break;
            case BTN_THUMBL: b = Button::L3; break;
            case BTN_THUMBR: b = Button::R3; break;
            case BTN_SELECT: b = Button::Select; break;
            case BTN_START: b = Button::Start; break;
            case BTN_MODE: b = Button::Mode; break;
            case BTN_DPAD_UP: b = Button::Up; break;
            case BTN_DPAD_DOWN: b = Button::Down; break;
            case BTN_DPAD_LEFT: b = Button::Left; break;
            case BTN_DPAD_RIGHT: b = Button::Right; break;
            default: break;
            }
            if (b) m.button(*b, down);
        }
    }

    void setupRumble() {
        if (!canWrite_) return;
        const auto ff = capBits(fd_, EV_FF, FF_MAX);
        if (!testBit(ff, FF_RUMBLE)) return;
        ff_effect fx{};
        fx.type = FF_RUMBLE;
        fx.id = -1;
        fx.replay.length = 35;
        fx.u.rumble.strong_magnitude = 0x1800;
        fx.u.rumble.weak_magnitude = 0x5000;
        if (::ioctl(fd_, EVIOCSFF, &fx) == 0) rumbleId_ = fx.id;
    }

    // DualSense / DS4: the lightbar is a multicolor LED named "inputN:rgb:indicator".
    void findLed() {
        std::error_code ec;
        const fs::path dev = fs::canonical(fs::path("/sys/class/input") / fs::path(path_).filename() / "device", ec);
        if (ec) return;
        const std::string prefix = dev.filename().string() + ":rgb";
        for (const auto& e : fs::directory_iterator("/sys/class/leds", ec)) {
            const std::string n = e.path().filename().string();
            if (n.rfind(prefix, 0) != 0) continue;
            const std::string dir = e.path().string();
            if (::access((dir + "/multi_intensity").c_str(), W_OK) == 0) {
                led_ = dir;
                std::ofstream(dir + "/brightness") << 255 << '\n';
            } else {
                ledHint_ = dir;
            }
            return;
        }
    }

    int fd_ = -1;
    bool canWrite_ = false;
    std::string path_, name_, led_, ledHint_;
    std::unordered_map<int, AxisRange> ranges_;
    int hatX_ = 0, hatY_ = 0;
    int rumbleId_ = -1;
    int lastRgb_[3] = {-1, -1, -1};
};

// ---------------------------------------------------------------- engine link

class Link {
public:
    explicit Link(std::string path) : path_(std::move(path)) {}
    ~Link() { if (fd_ >= 0) ::close(fd_); }

    bool connected() const { return fd_ >= 0; }
    int fd() const { return fd_; }

    bool connect() {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path_.c_str());
        if (fd_ < 0 || ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = -1;
            return false;
        }
        ::fcntl(fd_, F_SETFL, O_NONBLOCK);
        buf_.clear();
        return true;
    }

    void send(const sg::Json& m) {
        if (fd_ < 0) return;
        const std::string line = m.dump() + "\n";
        if (::send(fd_, line.data(), line.size(), MSG_NOSIGNAL) < 0 && errno != EAGAIN) drop();
    }

    /** Read whatever arrived; calls `on` per message. Returns false on disconnect. */
    template <class F> bool read(F&& on) {
        char tmp[65536];
        for (;;) {
            const ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
            if (n < 0) { if (errno == EAGAIN || errno == EINTR) break; drop(); return false; }
            if (n == 0) { drop(); return false; }
            buf_.append(tmp, static_cast<std::size_t>(n));
        }
        std::size_t nl;
        while ((nl = buf_.find('\n')) != std::string::npos) {
            const std::string line = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            try { on(sg::Json::parse(line)); } catch (...) {}
        }
        return true;
    }

private:
    void drop() { ::close(fd_); fd_ = -1; }
    std::string path_, buf_;
    int fd_ = -1;
};

sg::Json msg(const char* type) {
    sg::Json m = sg::Json::object();
    m.set("type", type);
    return m;
}

void handshake(Link& link) {
    sg::Json hello = msg("hello");
    hello.set("client", "sg-pad");
    hello.set("protocol", 1);
    link.send(hello);
    sg::Json sub = msg("subscribe");
    sub.set("status", true);
    link.send(sub);
    link.send(msg("getState"));
    link.send(msg("listPresets"));
}

fs::path envPath(const char* var, const char* fallbackUnderHome) {
    if (const char* v = std::getenv(var); v && *v) return v;
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "/tmp") / fallbackUnderHome;
}

void usage() {
    std::printf(
        "Usage: sg-pad [options]\n"
        "Play and tweak Soundgineer from a game controller.\n\n"
        "  --list            list connected game controllers\n"
        "  --device PATH     use this /dev/input/eventN (default: first controller found)\n"
        "  --config FILE     mapping file (default: $XDG_CONFIG_HOME/soundgineer/pad.json if present)\n"
        "  --print-config    print the default mapping as JSON (a starting point for pad.json)\n"
        "  --dry-run         print the messages instead of sending them to the engine\n"
        "  --socket PATH     engine socket (default: $XDG_RUNTIME_DIR/soundgineer.sock)\n"
        "  --version\n\n"
        "Default layout: left stick = macro 1/2 (push to turn, stays put), right stick =\n"
        "pitch bend + mod wheel, R2 = filter sweep, L2 = drive, face buttons + R1 = notes\n"
        "(minor pentatonic), L1 = sustain, d-pad up/down = octave, left/right = preset,\n"
        "L3/R3 = transpose, Options/Start = all notes off.\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string device, configPath, socketPath;
    bool dryRun = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version") { std::printf("sg-pad %s\n", sg::versionString()); return 0; }
        if (a == "-h" || a == "--help") { usage(); return 0; }
        if (a == "--print-config") { std::printf("%s\n", sgpad::Config::defaults().toJson().dump().c_str()); return 0; }
        if (a == "--list") {
            int found = 0;
            for (const auto& p : eventNodes()) {
                const int fd = ::open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) continue;
                if (isGamepad(fd)) { std::printf("%s\t%s\n", p.c_str(), deviceName(fd).c_str()); found++; }
                ::close(fd);
            }
            if (!found) std::fprintf(stderr, "no game controllers found (plugged in? readable /dev/input/event*?)\n");
            return found ? 0 : 1;
        }
        if (a == "--dry-run") { dryRun = true; continue; }
        if (a == "--device" && i + 1 < argc) { device = argv[++i]; continue; }
        if (a == "--config" && i + 1 < argc) { configPath = argv[++i]; continue; }
        if (a == "--socket" && i + 1 < argc) { socketPath = argv[++i]; continue; }
        std::fprintf(stderr, "sg-pad: unknown option %s\n", a.c_str());
        usage();
        return 2;
    }

    if (configPath.empty()) {
        const fs::path def = envPath("XDG_CONFIG_HOME", ".config") / "soundgineer" / "pad.json";
        if (fs::exists(def)) configPath = def.string();
    }
    sgpad::Config cfg = sgpad::Config::defaults();
    if (!configPath.empty()) {
        std::ifstream f(configPath);
        std::stringstream ss;
        ss << f.rdbuf();
        try { cfg = sgpad::Config::fromJson(sg::Json::parse(ss.str())); }
        catch (const std::exception& e) { std::fprintf(stderr, "sg-pad: %s: %s\n", configPath.c_str(), e.what()); return 2; }
    }
    if (socketPath.empty()) socketPath = (envPath("XDG_RUNTIME_DIR", ".cache") / "soundgineer.sock").string();

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Link link(socketPath);
    sgpad::Mapper mapper(cfg, [&](const sg::Json& m) {
        if (dryRun) { std::printf("%s\n", m.dump().c_str()); std::fflush(stdout); }
        else link.send(m);
    });

    Pad pad;
    using clock = std::chrono::steady_clock;
    auto lastTick = clock::now(), lastLight = lastTick, lastRetry = lastTick - std::chrono::seconds(5);
    bool warnedPad = false, warnedEngine = false;

    while (!gStop) {
        const auto now = clock::now();
        if (now - lastRetry > std::chrono::seconds(1)) {
            lastRetry = now;
            if (pad.fd() < 0) {
                if (device.empty() ? pad.openFirst() : pad.open(device)) {
                    std::fprintf(stderr, "sg-pad: %s (%s)%s%s\n", pad.name().c_str(), pad.path().c_str(),
                                 pad.hasRumble() && cfg.rumble ? ", rumble" : "",
                                 pad.hasLight() && cfg.lightbar ? ", lightbar" : "");
                    if (cfg.lightbar && !pad.hasLight() && !pad.ledHint().empty())
                        std::fprintf(stderr, "sg-pad: lightbar at %s is not writable; install "
                                             "packaging/udev/70-soundgineer-pad.rules to enable it\n",
                                     pad.ledHint().c_str());
                    warnedPad = false;
                } else if (!warnedPad) {
                    std::fprintf(stderr, "sg-pad: waiting for a game controller...\n");
                    warnedPad = true;
                }
            }
            if (!dryRun && !link.connected()) {
                if (link.connect()) { handshake(link); warnedEngine = false; std::fprintf(stderr, "sg-pad: connected to engine\n"); }
                else if (!warnedEngine) { std::fprintf(stderr, "sg-pad: waiting for %s ...\n", socketPath.c_str()); warnedEngine = true; }
            }
        }

        pollfd fds[2];
        int nfds = 0;
        if (pad.fd() >= 0) fds[nfds++] = {pad.fd(), POLLIN, 0};
        if (link.connected()) fds[nfds++] = {link.fd(), POLLIN, 0};
        ::poll(fds, static_cast<nfds_t>(nfds), 8);

        if (pad.fd() >= 0 && !pad.read(mapper)) {
            std::fprintf(stderr, "sg-pad: controller disconnected\n");
            pad.close();
            link.send(msg("allNotesOff"));
            mapper = sgpad::Mapper(cfg, [&](const sg::Json& m) {
                if (dryRun) { std::printf("%s\n", m.dump().c_str()); std::fflush(stdout); }
                else link.send(m);
            });
            if (link.connected()) handshake(link);
        }
        if (link.connected() && !link.read([&](const sg::Json& m) { mapper.onEngine(m); }))
            std::fprintf(stderr, "sg-pad: engine went away, retrying\n");

        const auto t = clock::now();
        const double dt = std::chrono::duration<double>(t - lastTick).count();
        if (dt >= 0.008) {
            mapper.tick(dt);
            lastTick = t;
            if (mapper.takeRumble()) pad.rumble();
        }
        if (cfg.lightbar && t - lastLight > std::chrono::milliseconds(33)) {
            pad.light(mapper.light());
            lastLight = t;
        }
    }

    link.send(msg("allNotesOff"));
    return 0;
}
