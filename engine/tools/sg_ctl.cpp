// sg-ctl: talk to a running soundgineer-engine over its Unix socket.

#include "sg/json.h"
#include "sg/version.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int connectTo(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path.c_str());
    if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::fprintf(stderr, "sg-ctl: cannot connect to %s (is soundgineer.socket enabled?)\n", path.c_str());
        std::exit(1);
    }
    return fd;
}

void sendLine(int fd, const sg::Json& msg) {
    const std::string line = msg.dump() + "\n";
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) < 0) { std::perror("sg-ctl"); std::exit(1); }
}

/** Read replies until `want` is seen or the timeout passes. Prints each reply's JSON if `print`. */
bool readUntil(int fd, const std::string& want, int timeoutMs, bool print, int count = 1) {
    std::string buf;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 50) <= 0) continue;
        char tmp[65536];
        const ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        buf.append(tmp, static_cast<std::size_t>(n));
        std::size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            const std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            try {
                const sg::Json j = sg::Json::parse(line);
                const std::string type = j["type"].asString();
                if (type == "error") { std::fprintf(stderr, "error: %s\n", j["message"].asString().c_str()); return false; }
                if (want.empty() || type == want) {
                    if (print) std::printf("%s\n", line.c_str());
                    if (--count <= 0) return true;
                }
            } catch (...) {}
        }
    }
    return want.empty();
}

sg::Json msg(const char* type) {
    sg::Json m = sg::Json::object();
    m.set("type", type);
    return m;
}

void usage() {
    std::printf(
        "Usage: sg-ctl [--socket PATH] <command>\n"
        "  hello                     engine version\n"
        "  state                     current patch and power state (JSON)\n"
        "  presets                   list presets (JSON)\n"
        "  load <name>               load a preset by name\n"
        "  save <name> [category]    save the current patch as a user preset\n"
        "  favorite <name> [on|off]  mark a preset as favorite\n"
        "  power on|off              start or release audio\n"
        "  param <id> <0..1>         set a normalized parameter\n"
        "  note <midi> [vel] [sec]   play a note (default 60 1.0 0.5)\n"
        "  watch [n]                 print n status messages (default 10)\n"
        "  send '<json>'             send one raw protocol message and print replies\n");
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::string socketPath = std::string(rt ? rt : "/tmp") + "/soundgineer.sock";
    if (args.size() >= 2 && args[0] == "--socket") { socketPath = args[1]; args.erase(args.begin(), args.begin() + 2); }
    if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return args.empty() ? 2 : 0; }
    if (args[0] == "--version") { std::printf("sg-ctl %s (protocol %d)\n", sg::versionString(), sg::kProtocolVersion); return 0; }

    const std::string& cmd = args[0];
    const int fd = connectTo(socketPath);
    bool ok = true;
    if (cmd == "hello") {
        sg::Json m = msg("hello");
        m.set("client", "sg-ctl");
        m.set("protocol", sg::kProtocolVersion);
        sendLine(fd, m);
        ok = readUntil(fd, "hello", 2000, true);
    } else if (cmd == "state") {
        sendLine(fd, msg("getState"));
        ok = readUntil(fd, "state", 2000, true);
    } else if (cmd == "presets") {
        sendLine(fd, msg("listPresets"));
        ok = readUntil(fd, "presetList", 2000, true);
    } else if (cmd == "load" && args.size() >= 2) {
        sg::Json m = msg("loadPreset");
        m.set("name", args[1]);
        sendLine(fd, m);
        ok = readUntil(fd, "state", 3000, false);
        if (ok) std::printf("loaded %s\n", args[1].c_str());
    } else if (cmd == "save" && args.size() >= 2) {
        sg::Json m = msg("savePreset");
        m.set("name", args[1]);
        if (args.size() >= 3) m.set("category", args[2]);
        sendLine(fd, m);
        ok = readUntil(fd, "saved", 2000, true);
    } else if (cmd == "favorite" && args.size() >= 2) {
        sg::Json m = msg("setFavorite");
        m.set("name", args[1]);
        m.set("on", args.size() < 3 || args[2] != "off");
        sendLine(fd, m);
        ok = readUntil(fd, "presetList", 2000, false);
    } else if (cmd == "power" && args.size() >= 2) {
        sg::Json m = msg("power");
        m.set("on", args[1] == "on");
        sendLine(fd, m);
        ok = readUntil(fd, "state", 3000, false);
    } else if (cmd == "param" && args.size() >= 3) {
        sg::Json m = msg("param");
        m.set("id", args[1]);
        m.set("value", std::atof(args[2].c_str()));
        sendLine(fd, m);
        ok = readUntil(fd, "", 300, true);
    } else if (cmd == "note" && args.size() >= 2) {
        sg::Json on = msg("noteOn");
        on.set("note", std::atoi(args[1].c_str()));
        on.set("velocity", args.size() >= 3 ? std::atof(args[2].c_str()) : 1.0);
        sendLine(fd, on);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000 * (args.size() >= 4 ? std::atof(args[3].c_str()) : 0.5))));
        sg::Json off = msg("noteOff");
        off.set("note", std::atoi(args[1].c_str()));
        sendLine(fd, off);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else if (cmd == "watch") {
        sg::Json m = msg("subscribe");
        m.set("status", true);
        sendLine(fd, m);
        ok = readUntil(fd, "status", 10000, true, args.size() >= 2 ? std::atoi(args[1].c_str()) : 10);
    } else if (cmd == "send" && args.size() >= 2) {
        try { sendLine(fd, sg::Json::parse(args[1])); } catch (const std::exception& e) { std::fprintf(stderr, "sg-ctl: %s\n", e.what()); return 2; }
        ok = readUntil(fd, "", 500, true);
    } else {
        usage();
        ::close(fd);
        return 2;
    }
    ::close(fd);
    return ok ? 0 : 1;
}
