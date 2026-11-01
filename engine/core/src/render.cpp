#include "sg/render.h"

#include "sg/processor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstring>
#include <stdexcept>

namespace sg {

Rendered renderOffline(const Patch& patch, const std::vector<NoteEvent>& notes, double seconds,
                       double sampleRate, std::uint32_t seed, WavetableBank& bank) {
    auto proc = std::make_unique<Processor>(sampleRate, seed);
    syncPatch(*proc, patch, bank);

    struct Ev { long block; int kind; int note; double vel; };
    std::vector<Ev> events;
    for (const auto& n : notes) {
        events.push_back({static_cast<long>(std::floor(n.on * sampleRate / kBlockSize)), 1, n.note, n.velocity});
        events.push_back({static_cast<long>(std::floor(n.off * sampleRate / kBlockSize)), 0, n.note, 0});
    }
    std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) {
        return a.block != b.block ? a.block < b.block : a.kind < b.kind;
    });

    const long blocks = static_cast<long>(std::ceil(seconds * sampleRate / kBlockSize));
    Rendered out;
    out.left.resize(static_cast<std::size_t>(blocks) * kBlockSize);
    out.right.resize(out.left.size());
    std::size_t e = 0;
    for (long b = 0; b < blocks; b++) {
        while (e < events.size() && events[e].block == b) {
            const Ev& ev = events[e++];
            if (ev.kind) proc->noteOn(ev.note, ev.vel);
            else proc->noteOff(ev.note);
        }
        proc->process(out.left.data() + b * kBlockSize, out.right.data() + b * kBlockSize);
    }
    return out;
}

bool writeWavFloat(const char* path, const Rendered& a, int sr) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const std::uint32_t n = static_cast<std::uint32_t>(a.left.size());
    auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + n * 8); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(3); u16(2); u32(static_cast<std::uint32_t>(sr));
    u32(static_cast<std::uint32_t>(sr) * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(n * 8);
    for (std::uint32_t i = 0; i < n; i++) { std::fwrite(&a.left[i], 4, 1, f); std::fwrite(&a.right[i], 4, 1, f); }
    return std::fclose(f) == 0;
}

Rendered readWav(const char* path, int* sampleRate) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("cannot open ") + path);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 44) throw std::runtime_error("short WAV");
    auto u16 = [&](std::size_t o) { return static_cast<unsigned>(b[o] | (b[o + 1] << 8)); };
    auto u32 = [&](std::size_t o) { return static_cast<std::uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)) | (static_cast<std::uint32_t>(b[o + 3]) << 24); };
    unsigned fmt = 0, ch = 0, bits = 0;
    std::size_t off = 12, data = 0, len = 0;
    while (off + 8 <= b.size()) {
        const std::uint32_t sz = u32(off + 4);
        if (std::equal(b.begin() + static_cast<long>(off), b.begin() + static_cast<long>(off) + 4, "fmt ")) {
            fmt = u16(off + 8); ch = u16(off + 10); if (sampleRate) *sampleRate = static_cast<int>(u32(off + 12)); bits = u16(off + 22);
        } else if (std::equal(b.begin() + static_cast<long>(off), b.begin() + static_cast<long>(off) + 4, "data")) {
            data = off + 8; len = std::min<std::size_t>(sz, b.size() - data);
        }
        off += 8 + sz + (sz & 1);
    }
    if (!data || !ch || !((fmt == 3 && bits == 32) || (fmt == 1 && bits == 16))) throw std::runtime_error("unsupported WAV");
    const std::size_t bytes = bits / 8, frames = len / (bytes * ch);
    Rendered out;
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t i = 0; i < frames; i++)
        for (unsigned c = 0; c < std::min(ch, 2u); c++) {
            const std::size_t o = data + (i * ch + c) * bytes;
            float v;
            if (bits == 32) std::memcpy(&v, &b[o], 4);
            else v = static_cast<float>(static_cast<std::int16_t>(u16(o)) / 32768.0);
            (c == 0 ? out.left : out.right)[i] = v;
            if (ch == 1) out.right[i] = v;
        }
    return out;
}

} // namespace sg
