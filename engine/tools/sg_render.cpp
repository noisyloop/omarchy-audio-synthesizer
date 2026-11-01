// sg-render: render a preset and a MIDI file to WAV with no audio device.
// Phase 1 stub; the renderer lands with the DSP port (build prompt, Phase 3).

#include "sg/version.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc > 1 && (std::strcmp(argv[1], "--version") == 0)) {
        std::printf("sg-render %s\n", sg::versionString());
        return 0;
    }
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf("Usage: sg-render --preset <file.json> --midi <file.mid> --out <file.wav>\n"
                    "                 [--rate 48000] [--seed N] [--tail SECONDS]\n");
        return 0;
    }
    std::fprintf(stderr, "sg-render: not implemented yet (DSP port in progress)\n");
    return 2;
}
