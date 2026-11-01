#pragma once

// Port of the reference src/shared/wavetable-gen.ts.

#include <cstdint>
#include <string>
#include <vector>

namespace sg {

inline constexpr int kFrameSize = 2048;
inline constexpr int kNumMips = 11;

struct Wavetable {
    std::string name;
    int frameSize = kFrameSize;
    int numFrames = 0;
    std::vector<float> data;  // numFrames * frameSize
};

/** Layout: [(frame * kNumMips + mip) * frameSize ...]. */
std::vector<float> buildMips(const float* data, int frameSize, int numFrames);
std::vector<float> spectralMorph(const std::vector<std::vector<float>>& keyFrames, int numFrames);
Wavetable generateWavetable(const std::string& name);

struct DecodedWav {
    int sampleRate = 0;
    std::vector<float> channelData;  // mono mixdown
};
/** Throws std::runtime_error on malformed input. */
DecodedWav decodeWav(const std::uint8_t* bytes, std::size_t size);
Wavetable wavToWavetable(const std::string& name, const DecodedWav& wav);

} // namespace sg
