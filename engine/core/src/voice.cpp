#include "sg/voice.h"

#include "sg/jsmath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sg {

namespace {

int idx(const char* id) { return paramIndex(id); }

struct OscIdx { int enabled, wavetable, morph, level, pan, unison, detune, blend, spread, phase, phaseRand, transpose, fine, sync; };
struct FiltIdx { int enabled, type, cutoff, resonance, drive, keytrack, mix; };
struct EnvIdx { int delay, attack, hold, decay, sustain, release, atkCurve, decCurve, relCurve; };
struct LfoIdx { int rate, sync, division, mode, phase, smooth; };

struct Indices {
    OscIdx osc[3];
    int subEnabled, subShape, subLevel, subPan, subOctave;
    int noiseEnabled, noiseType, noiseLevel, noisePan, noisePitch;
    FiltIdx filt[2];
    int routing;
    int distType, distDrive, distMix, distBits, distDown;
    EnvIdx env[6];
    LfoIdx lfo[8];
    int macro[4];

    Indices() {
        char b[64];
        auto id = [&](const char* fmt, int n, const char* field) {
            std::snprintf(b, sizeof b, fmt, n, field);
            return idx(b);
        };
        for (int o = 0; o < 3; o++) {
            const char* f = "osc%d.%s";
            osc[o] = {id(f, o + 1, "enabled"), id(f, o + 1, "wavetable"), id(f, o + 1, "morph"), id(f, o + 1, "level"),
                      id(f, o + 1, "pan"), id(f, o + 1, "unison"), id(f, o + 1, "detune"), id(f, o + 1, "blend"),
                      id(f, o + 1, "spread"), id(f, o + 1, "phase"), id(f, o + 1, "phase_rand"), id(f, o + 1, "transpose"),
                      id(f, o + 1, "fine"), id(f, o + 1, "sync")};
        }
        subEnabled = idx("sub.enabled"); subShape = idx("sub.shape"); subLevel = idx("sub.level");
        subPan = idx("sub.pan"); subOctave = idx("sub.octave");
        noiseEnabled = idx("noise.enabled"); noiseType = idx("noise.type"); noiseLevel = idx("noise.level");
        noisePan = idx("noise.pan"); noisePitch = idx("noise.pitch");
        for (int f = 0; f < 2; f++) {
            const char* p = "filter%d.%s";
            filt[f] = {id(p, f + 1, "enabled"), id(p, f + 1, "type"), id(p, f + 1, "cutoff"), id(p, f + 1, "resonance"),
                       id(p, f + 1, "drive"), id(p, f + 1, "keytrack"), id(p, f + 1, "mix")};
        }
        routing = idx("filter.routing");
        distType = idx("dist.type"); distDrive = idx("dist.drive"); distMix = idx("dist.mix");
        distBits = idx("dist.bits"); distDown = idx("dist.downsample");
        for (int e = 0; e < 6; e++) {
            const char* p = "env%d.%s";
            env[e] = {id(p, e + 1, "delay"), id(p, e + 1, "attack"), id(p, e + 1, "hold"), id(p, e + 1, "decay"),
                      id(p, e + 1, "sustain"), id(p, e + 1, "release"), id(p, e + 1, "atk_curve"),
                      id(p, e + 1, "dec_curve"), id(p, e + 1, "rel_curve")};
        }
        for (int l = 0; l < 8; l++) {
            const char* p = "lfo%d.%s";
            lfo[l] = {id(p, l + 1, "rate"), id(p, l + 1, "sync"), id(p, l + 1, "division"), id(p, l + 1, "mode"),
                      id(p, l + 1, "phase"), id(p, l + 1, "smooth")};
        }
        for (int m = 0; m < 4; m++) macro[m] = id("macro%d.%s", m + 1, "value");
    }
};

const Indices& I() {
    static const Indices instance;
    return instance;
}

} // namespace


Voice::Voice(double sr, int blockSize)
    : sr_(sr),
      envs_{{sr, blockSize}, {sr, blockSize}, {sr, blockSize}, {sr, blockSize}, {sr, blockSize}, {sr, blockSize}},
      lfos_{{sr, blockSize}, {sr, blockSize}, {sr, blockSize}, {sr, blockSize},
            {sr, blockSize}, {sr, blockSize}, {sr, blockSize}, {sr, blockSize}},
      filters_{VoiceFilter(sr), VoiceFilter(sr)} {
    (void)I();
}

void Voice::noteOn(int n, double vel, double a, const VoiceContext& ctx) noexcept {
    note = n;
    velocity = vel;
    gate = true;
    sustained = false;
    age = a;
    random_ = ctx.rng->next();
    samplePos_ = 0;
    prevAmp_ = 0;
    subPhase_ = 0;
    distHoldL_ = distHoldR_ = 0;
    distCount_ = 0;

    for (int o = 0; o < 3; o++) {
        const double startPhase = ctx.base[I().osc[o].phase];
        const double rand = ctx.base[I().osc[o].phaseRand];
        for (int u = 0; u < kMaxUnison; u++)
            phases_[o * kMaxUnison + u] = jsMod(startPhase + rand * ctx.rng->next(), 1);
    }
    for (auto& e : envs_) e.trigger();
    for (int l = 0; l < 8; l++) lfos_[l].trigger(ctx.base[I().lfo[l].phase]);
    for (auto& f : filters_) f.reset();
    std::memset(modOffsets_, 0, sizeof modOffsets_);
    std::memset(sources, 0, sizeof sources);
    sources[kSrcVelocity] = static_cast<float>(vel);
    sources[kSrcKeytrack] = static_cast<float>(std::max(-1.0, std::min(1.0, (n - 60) / 36.0)));
    sources[kSrcRandom] = static_cast<float>(random_);
}

void Voice::noteOff() noexcept {
    gate = false;
    sustained = false;
    for (auto& e : envs_) e.gateOff();
}

void Voice::clearMods() noexcept { std::memset(modOffsets_, 0, sizeof modOffsets_); }

void Voice::kill() noexcept {
    for (auto& e : envs_) e.kill();
}

double Voice::pv(int index, const VoiceContext& ctx) const noexcept {
    return normToValue(kParams[index], static_cast<double>(ctx.base[index]) + static_cast<double>(modOffsets_[index]));
}

void Voice::computeMods(const VoiceContext& ctx) noexcept {
    for (int m = 0; m < 4; m++) sources[kSrcMacro0 + m] = ctx.base[I().macro[m]];
    sources[kSrcModwheel] = static_cast<float>(ctx.modWheel);
    sources[kSrcPitchwheel] = static_cast<float>(ctx.pitchBend);
    sources[kSrcAftertouch] = static_cast<float>(ctx.aftertouch);
    const ModRoutes& R = *ctx.routes;
    for (int d = 0; d < R.destCount; d++) {
        double sum = 0;
        for (int i = 0; i < R.count[d]; i++) sum += R.routes[d][i].depth * static_cast<double>(sources[R.routes[d][i].source]);
        modOffsets_[R.dests[d]] = static_cast<float>(sum);
    }
}

void Voice::advanceModulators(const VoiceContext& ctx) noexcept {
    EnvParams p;
    for (int e = 0; e < 6; e++) {
        const EnvIdx& ix = I().env[e];
        p.delay = pv(ix.delay, ctx);
        p.attack = pv(ix.attack, ctx);
        p.hold = pv(ix.hold, ctx);
        p.decay = pv(ix.decay, ctx);
        p.sustain = pv(ix.sustain, ctx);
        p.release = pv(ix.release, ctx);
        p.atkCurve = pv(ix.atkCurve, ctx);
        p.decCurve = pv(ix.decCurve, ctx);
        p.relCurve = pv(ix.relCurve, ctx);
        sources[kSrcEnv0 + e] = static_cast<float>(envs_[e].process(p));
    }
    for (int l = 0; l < 8; l++) {
        const LfoIdx& ix = I().lfo[l];
        const int mode = static_cast<int>(jsRound(normToValue(kParams[ix.mode], ctx.base[ix.mode])));
        const double smooth = ctx.base[ix.smooth];
        sources[kSrcLfo0 + l] = static_cast<float>(lfos_[l].process(
            ctx.lfoShapes[l], ctx.lfoFreqs[l], mode, ctx.lfoGlobalPhases[l], ctx.lfoBeatPhases[l], smooth));
    }
}

void Voice::runFilter(int f, float* l, float* r, int n, const VoiceContext& ctx) noexcept {
    const FiltIdx& ix = I().filt[f];
    const int type = static_cast<int>(jsRound(normToValue(kParams[ix.type], ctx.base[ix.type])));
    const double kt = pv(ix.keytrack, ctx);
    const double keyOffset = note - 60;
    const double cutoff = pv(ix.cutoff, ctx) * std::pow(2, (keyOffset / 12) * kt);
    filters_[f].process(l, r, n, type, cutoff, pv(ix.resonance, ctx), pv(ix.drive, ctx), pv(ix.mix, ctx));
}

bool Voice::render(const VoiceContext& ctx, float* outL, float* outR, int n) noexcept {
    computeMods(ctx);
    advanceModulators(ctx);
    if (!active()) return false;

    float* bufL = bufL_;
    float* bufR = bufR_;
    std::fill(bufL, bufL + n, 0.0f);
    std::fill(bufR, bufR + n, 0.0f);

    const double baseFreq = noteToFreq(note + ctx.pitchBend * ctx.bendRange);

    // ---- wavetable oscillators
    for (int o = 0; o < 3; o++) {
        const OscIdx& ix = I().osc[o];
        if (ctx.base[ix.enabled] < 0.5f) continue;
        const WavetableData* table = ctx.tables[o];
        if (!table) continue;

        const double morph = std::max(0.0, std::min(1.0, static_cast<double>(ctx.base[ix.morph]) + static_cast<double>(modOffsets_[ix.morph])));
        const double level = pv(ix.level, ctx);
        if (level <= 0.0001) continue;
        const double pan = pv(ix.pan, ctx);
        const int unison = static_cast<int>(std::max(1.0, std::min(static_cast<double>(kMaxUnison), jsRound(pv(ix.unison, ctx)))));
        const double detune = pv(ix.detune, ctx);
        const double blend = pv(ix.blend, ctx);
        const double spread = pv(ix.spread, ctx);
        const double transpose = pv(ix.transpose, ctx);
        const double fine = pv(ix.fine, ctx);
        const double sync = pv(ix.sync, ctx);
        const double freq = baseFreq * std::pow(2, (transpose + fine / 100) / 12);

        double norm = 0;
        for (int u = 0; u < unison; u++) {
            const double off = unison == 1 ? 0 : (2.0 * u) / (unison - 1) - 1;
            const double w = (1 - blend) * (1 - std::fabs(off)) + blend;
            norm += w * w;
        }
        norm = 1 / std::sqrt(std::max(norm, 1e-9));

        for (int u = 0; u < unison; u++) {
            const double off = unison == 1 ? 0 : (2.0 * u) / (unison - 1) - 1;
            const double ratio = std::pow(2, (detune * off) / 1200);
            const double inc = (freq * ratio) / sr_;
            if (inc >= 0.5) continue;
            const double w = ((1 - blend) * (1 - std::fabs(off)) + blend) * norm * level;
            const double p = std::max(-1.0, std::min(1.0, pan + spread * off * 0.9));
            const double gl = w * std::cos(((p + 1) * kPi) / 4);
            const double gr = w * std::sin(((p + 1) * kPi) / 4);
            const int mip = table->mipForInc(inc * sync);
            double phase = phases_[o * kMaxUnison + u];
            if (sync <= 1.001) {
                for (int i = 0; i < n; i++) {
                    phase += inc;
                    if (phase >= 1) phase -= 1;
                    const double s = table->read(morph, phase, mip);
                    bufL[i] = static_cast<float>(bufL[i] + s * gl);
                    bufR[i] = static_cast<float>(bufR[i] + s * gr);
                }
            } else {
                for (int i = 0; i < n; i++) {
                    phase += inc;
                    if (phase >= 1) phase -= 1;
                    const double sp = jsMod(phase * sync, 1);
                    const double s = table->read(morph, sp, mip);
                    bufL[i] = static_cast<float>(bufL[i] + s * gl);
                    bufR[i] = static_cast<float>(bufR[i] + s * gr);
                }
            }
            phases_[o * kMaxUnison + u] = phase;
        }
    }

    // ---- sub oscillator
    if (ctx.base[I().subEnabled] >= 0.5f) {
        const double level = pv(I().subLevel, ctx);
        if (level > 0.0001) {
            const double pan = pv(I().subPan, ctx);
            const int shape = static_cast<int>(jsRound(normToValue(kParams[I().subShape], ctx.base[I().subShape])));
            const double oct = pv(I().subOctave, ctx);
            const double inc = (baseFreq * std::pow(2, oct)) / sr_;
            const double gl = level * std::cos(((pan + 1) * kPi) / 4);
            const double gr = level * std::sin(((pan + 1) * kPi) / 4);
            double ph = subPhase_;
            for (int i = 0; i < n; i++) {
                ph += inc;
                if (ph >= 1) ph -= 1;
                double s;
                switch (shape) {
                case 1: s = 1 - 4 * std::fabs(ph - 0.5); break;
                case 2: s = 2 * ph - 1; break;
                case 3: s = ph < 0.5 ? 1 : -1; break;
                default: s = std::sin(ph * 2 * kPi);
                }
                bufL[i] = static_cast<float>(bufL[i] + s * gl);
                bufR[i] = static_cast<float>(bufR[i] + s * gr);
            }
            subPhase_ = ph;
        }
    }

    // ---- noise oscillator / sample slot
    if (ctx.base[I().noiseEnabled] >= 0.5f) {
        const double level = pv(I().noiseLevel, ctx);
        if (level > 0.0001) {
            const double pan = pv(I().noisePan, ctx);
            const int type = static_cast<int>(jsRound(normToValue(kParams[I().noiseType], ctx.base[I().noiseType])));
            const double gl = level * std::cos(((pan + 1) * kPi) / 4);
            const double gr = level * std::sin(((pan + 1) * kPi) / 4);
            if (type == 2 && ctx.sample && !ctx.sample->data.empty()) {
                const double pitch = pv(I().noisePitch, ctx);
                const double rate = std::pow(2, pitch / 12) * (ctx.sample->sampleRate / sr_);
                const auto& data = ctx.sample->data;
                const double len = static_cast<double>(data.size());
                double pos = samplePos_;
                for (int i = 0; i < n; i++) {
                    const std::size_t i0 = static_cast<std::size_t>(pos);
                    const double s = data[i0 % data.size()];
                    bufL[i] = static_cast<float>(bufL[i] + s * gl);
                    bufR[i] = static_cast<float>(bufR[i] + s * gr);
                    pos += rate;
                    if (pos >= len) pos -= len;
                }
                samplePos_ = pos;
            } else if (type == 1) {
                double* pk = pink_;
                for (int i = 0; i < n; i++) {
                    const double white = ctx.rng->next() * 2 - 1;
                    pk[0] = 0.99765 * pk[0] + white * 0.099046;
                    pk[1] = 0.963 * pk[1] + white * 0.2965164;
                    pk[2] = 0.57 * pk[2] + white * 1.0526913;
                    const double s = (pk[0] + pk[1] + pk[2] + white * 0.1848) * 0.2;
                    bufL[i] = static_cast<float>(bufL[i] + s * gl);
                    bufR[i] = static_cast<float>(bufR[i] + s * gr);
                }
            } else {
                for (int i = 0; i < n; i++) {
                    const double s = ctx.rng->next() * 2 - 1;
                    bufL[i] = static_cast<float>(bufL[i] + s * gl);
                    bufR[i] = static_cast<float>(bufR[i] + s * gr);
                }
            }
        }
    }

    // ---- filters
    const int routing = static_cast<int>(jsRound(normToValue(kParams[I().routing], ctx.base[I().routing])));
    const bool f1on = ctx.base[I().filt[0].enabled] >= 0.5f;
    const bool f2on = ctx.base[I().filt[1].enabled] >= 0.5f;
    if (routing == 0) {
        if (f1on) runFilter(0, bufL, bufR, n, ctx);
        if (f2on) runFilter(1, bufL, bufR, n, ctx);
    } else if (f1on || f2on) {
        if (f1on && f2on) {
            std::copy(bufL, bufL + n, tmpL_);
            std::copy(bufR, bufR + n, tmpR_);
            runFilter(0, bufL, bufR, n, ctx);
            runFilter(1, tmpL_, tmpR_, n, ctx);
            for (int i = 0; i < n; i++) {
                bufL[i] = static_cast<float>((static_cast<double>(bufL[i]) + tmpL_[i]) * 0.5);
                bufR[i] = static_cast<float>((static_cast<double>(bufR[i]) + tmpR_[i]) * 0.5);
            }
        } else {
            runFilter(f1on ? 0 : 1, bufL, bufR, n, ctx);
        }
    }

    // ---- distortion section
    const int distType = static_cast<int>(jsRound(normToValue(kParams[I().distType], ctx.base[I().distType])));
    if (distType > 0) {
        const double drive = pv(I().distDrive, ctx);
        const double mix = pv(I().distMix, ctx);
        const double gain = 1 + drive * 15;
        const double comp = 1 / std::pow(gain, 0.5);
        if (distType == 4) {
            const double bits = pv(I().distBits, ctx);
            const double levels = std::pow(2, bits);
            const double down = std::max(1.0, jsRound(pv(I().distDown, ctx)));
            for (int i = 0; i < n; i++) {
                if (distCount_ <= 0) {
                    distHoldL_ = jsRound(bufL[i] * levels) / levels;
                    distHoldR_ = jsRound(bufR[i] * levels) / levels;
                    distCount_ = down;
                }
                distCount_--;
                bufL[i] = static_cast<float>(bufL[i] * (1 - mix) + distHoldL_ * mix);
                bufR[i] = static_cast<float>(bufR[i] * (1 - mix) + distHoldR_ * mix);
            }
        } else {
            for (int i = 0; i < n; i++) {
                const double xl = bufL[i], xr = bufR[i];
                double sl, sr2;
                if (distType == 1) {
                    sl = std::tanh(xl * gain) * comp;
                    sr2 = std::tanh(xr * gain) * comp;
                } else if (distType == 2) {
                    sl = std::max(-0.8, std::min(0.8, xl * gain)) * comp;
                    sr2 = std::max(-0.8, std::min(0.8, xr * gain)) * comp;
                } else {
                    sl = std::sin(xl * gain * 1.5) * comp;
                    sr2 = std::sin(xr * gain * 1.5) * comp;
                }
                bufL[i] = static_cast<float>(xl * (1 - mix) + sl * mix);
                bufR[i] = static_cast<float>(xr * (1 - mix) + sr2 * mix);
            }
        }
    }

    // ---- amplitude envelope + velocity
    const double velGain = 0.25 + 0.75 * velocity;
    const double targetAmp = static_cast<double>(sources[kSrcEnv0]) * velGain;
    const double ampStep = (targetAmp - prevAmp_) / n;
    double amp = prevAmp_;
    for (int i = 0; i < n; i++) {
        amp += ampStep;
        outL[i] = static_cast<float>(outL[i] + bufL[i] * amp);
        outR[i] = static_cast<float>(outR[i] + bufR[i] * amp);
    }
    prevAmp_ = targetAmp;
    return active();
}

} // namespace sg
