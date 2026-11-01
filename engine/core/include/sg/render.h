#pragma once

// Offline rendering (no audio device): drives the Processor block by block
// with note events quantized to block boundaries, exactly like the parity
// harness does with the reference.

#include "sg/patch.h"

#include <vector>

namespace sg {

struct NoteEvent {
    double on;    // seconds
    double off;   // seconds
    int note;
    double velocity;
};

struct Rendered {
    std::vector<float> left, right;
};

Rendered renderOffline(const Patch& patch, const std::vector<NoteEvent>& notes, double seconds,
                       double sampleRate, std::uint32_t seed, WavetableBank& bank);

/** Write float32 stereo WAV. Returns false on I/O error. */
bool writeWavFloat(const char* path, const Rendered& audio, int sampleRate);
/** Read a float32 or 16-bit stereo/mono WAV into left/right. Throws on error. */
Rendered readWav(const char* path, int* sampleRate);

} // namespace sg
