// sg-ctl: talk to a running soundgineer-engine over its Unix socket.
// Phase 1 stub; the protocol client lands with the realtime engine (Phase 4).

#include "sg/version.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("sg-ctl %s (protocol %d)\n", sg::versionString(), sg::kProtocolVersion);
        return 0;
    }
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf("Usage: sg-ctl <command> [args]\n"
                    "  send '<json>'      send one protocol message\n"
                    "  note <n> [vel]     play a note\n"
                    "  load <preset>      load a preset by name or path\n"
                    "  watch              print telemetry\n");
        return 0;
    }
    std::fprintf(stderr, "sg-ctl: not implemented yet (realtime engine in progress)\n");
    return 2;
}
