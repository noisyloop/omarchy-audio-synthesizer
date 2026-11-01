// soundgineer-engine: headless synth daemon.
// Phase 1 stub. It proves the JACK/PipeWire client stack links and can see
// the audio graph; the realtime engine, socket server and presets land in
// Phase 4.

#include "sg/version.h"

#include <jack/jack.h>

#include <cstdio>
#include <cstring>

namespace {

void printPorts(jack_client_t* client, const char* type, unsigned long flags, const char* label) {
    const char** ports = jack_get_ports(client, nullptr, type, flags);
    if (!ports) return;
    for (const char** p = ports; *p; ++p) std::printf("%s %s\n", label, *p);
    jack_free(ports);
}

void quiet(const char*) {}

int listPorts() {
    // libjack prints its own connection chatter to stderr; keep the probe readable.
    jack_set_error_function(quiet);
    jack_set_info_function(quiet);
    jack_status_t status{};
    jack_client_t* client =
        jack_client_open("soundgineer-probe", JackNoStartServer, &status);
    if (!client) {
        std::fprintf(stderr,
                     "soundgineer-engine: no PipeWire/JACK server reachable "
                     "(is pipewire-jack installed and PipeWire running?)\n");
        return 1;
    }
    std::printf("server sample rate: %u\n", jack_get_sample_rate(client));
    std::printf("server buffer size: %u\n", jack_get_buffer_size(client));
    printPorts(client, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput | JackPortIsPhysical, "audio out:");
    printPorts(client, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, "midi in:");
    jack_client_close(client);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("soundgineer-engine %s (protocol %d, %s)\n", sg::versionString(),
                    sg::kProtocolVersion, jack_get_version_string());
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--list-ports") == 0)
        return listPorts();
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf("Usage: soundgineer-engine [--version | --list-ports]\n");
        return 0;
    }
    std::fprintf(stderr, "soundgineer-engine: realtime engine not implemented yet\n");
    return 2;
}
