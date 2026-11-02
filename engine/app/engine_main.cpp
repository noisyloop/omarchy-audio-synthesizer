// soundgineer-engine: headless synth daemon. Audio and MIDI through the JACK
// API (pipewire-jack); control over a private Unix socket (docs/protocol.md).

#include "engine.h"
#include "server.h"

#include "sg/version.h"

#include <jack/jack.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

volatile bool gStop = false;
void onSignal(int) { gStop = true; }

fs::path envPath(const char* var, const char* fallbackUnderHome) {
    if (const char* v = std::getenv(var); v && *v) return v;
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "/tmp") / fallbackUnderHome;
}

fs::path factoryDir(const char* argv0) {
    if (const char* v = std::getenv("SG_FACTORY_PRESETS"); v && *v) return v;
    std::error_code ec;
    const fs::path exe = fs::canonical("/proc/self/exe", ec);
    const fs::path base = ec ? fs::absolute(argv0).parent_path() : exe.parent_path();
    if (fs::exists(base / "../share/soundgineer/factory")) return fs::weakly_canonical(base / "../share/soundgineer/factory");
    for (fs::path p = base; !p.empty() && p != p.root_path(); p = p.parent_path())
        if (fs::exists(p / "presets/factory")) return p / "presets/factory";
    return envPath("XDG_DATA_HOME", ".local/share") / "soundgineer/factory";
}

void quiet(const char*) {}

int listPorts() {
    jack_set_error_function(quiet);
    jack_set_info_function(quiet);
    jack_status_t status{};
    jack_client_t* client = jack_client_open("soundgineer-probe", JackNoStartServer, &status);
    if (!client) {
        std::fprintf(stderr, "soundgineer-engine: no PipeWire/JACK server reachable (is pipewire-jack installed and PipeWire running?)\n");
        return 1;
    }
    std::printf("server sample rate: %u\nserver buffer size: %u\n", jack_get_sample_rate(client), jack_get_buffer_size(client));
    struct Query { const char* type; unsigned long flags; const char* label; };
    const Query queries[] = {{JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsPhysical, "audio out:"},
                             {JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, "midi in:"}};
    for (const auto& q : queries) {
        if (const char** ports = jack_get_ports(client, nullptr, q.type, q.flags)) {
            for (const char** p = ports; *p; ++p) std::printf("%s %s\n", q.label, *p);
            jack_free(ports);
        }
    }
    jack_client_close(client);
    return 0;
}

void usage() {
    std::printf("Usage: soundgineer-engine [--socket PATH] [--dummy-audio] [--version] [--list-ports]\n"
                "  Serves the control protocol on $XDG_RUNTIME_DIR/soundgineer.sock (0600),\n"
                "  or on the socket systemd passes in (soundgineer.socket).\n"
                "  --dummy-audio  run the DSP on a timer without a sound server (testing)\n");
}

} // namespace

int main(int argc, char** argv) {
    bool dummy = false;
    std::string socketPath;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--version") {
            std::printf("soundgineer-engine %s (protocol %d, %s)\n", sg::versionString(), sg::kProtocolVersion, jack_get_version_string());
            return 0;
        }
        if (a == "--list-ports") return listPorts();
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (a == "--dummy-audio") dummy = true;
        else if (a == "--socket" && i + 1 < argc) socketPath = argv[++i];
        else { usage(); return 2; }
    }

    // libjack prints connection chatter to stderr; keep the journal readable.
    jack_set_error_function(quiet);
    jack_set_info_function(quiet);

    sg::Paths paths;
    paths.socket = socketPath.empty() ? envPath("XDG_RUNTIME_DIR", ".cache") / "soundgineer.sock" : fs::path(socketPath);
    paths.factoryPresets = factoryDir(argv[0]);
    paths.userPresets = envPath("XDG_DATA_HOME", ".local/share") / "soundgineer/presets";
    paths.state = envPath("XDG_STATE_HOME", ".local/state") / "soundgineer";
    paths.userData = envPath("XDG_DATA_HOME", ".local/share") / "soundgineer";
    paths.music = envPath("XDG_MUSIC_DIR", "Music");

    // systemd socket activation (soundgineer.socket): fd 3.
    int inherited = -1;
    if (const char* pid = std::getenv("LISTEN_PID"); pid && std::atoi(pid) == ::getpid())
        if (const char* n = std::getenv("LISTEN_FDS"); n && std::atoi(n) >= 1) inherited = 3;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    sg::Engine engine;
    sg::Server server(engine, paths, dummy);
    std::string err;
    if (!server.listen(inherited, err)) {
        std::fprintf(stderr, "soundgineer-engine: cannot listen on %s: %s\n", paths.socket.c_str(), err.c_str());
        return 1;
    }
    server.restoreSession();
    std::fprintf(stderr, "soundgineer-engine %s: listening on %s, %zu factory presets built in, user presets in %s\n",
                 sg::versionString(), inherited >= 0 ? "systemd socket" : paths.socket.c_str(),
                 sg::Server::factory().size(), paths.userPresets.c_str());
    server.run(gStop);
    server.saveSession();
    engine.stop();
    return 0;
}
