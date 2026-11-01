# Porting notes

Record of how the native engine relates to the reference TypeScript
implementation: what was ported from where, deliberate deviations, and
reference bugs that are matched rather than fixed.

## Reference version

Ported from the Soundgineer TypeScript reference at commit `926f3e1`.
Every parity fixture records this commit; bump it here and regenerate
fixtures together.

## Porting rules

1. One reference file maps to one core file: `dsp.ts` → `dsp.{h,cpp}`,
   `voice.ts` → `voice`, `effects.ts` → `effects`, `processor.ts` →
   `processor`, `shared/*.ts` → `params`, `fft`, `wavetable_gen`, `messages`;
   main-thread patch logic in `audio/engine.ts` → `patch`, `ui/preset-io.ts`
   → `preset_io`.
2. Same class names, parameter IDs, ranges, curves, smoothing and order of
   operations.
3. JS `number` becomes `double`. `Float32Array` storage becomes `float`.
   Rounding happens where the reference rounds (on store into a typed
   array), nowhere else.
4. The DSP runs in fixed 128-sample control blocks (`kBlockSize`) whatever the
   host buffer size. Hosts with other buffer sizes are adapted by an
   internal FIFO, never by changing the control rate.
5. `Math.random()` becomes `sg::Rng::next()` (mulberry32), called at the
   same three sites in the same order: per-note random value and unison
   start-phase randomization at note-on, and the noise oscillator.
6. Core code is compiled with `-fno-fast-math -ffp-contract=off` so the
   compiler does not reassociate or fuse operations the reference does not.

## Parity result

All 53 cases in `tests/parity/cases.json` match the reference **bit for bit**
(residual reported as −400 dB, i.e. zero difference) at 44.1 kHz with seed
12345. That covers the init patch at C3/C4/C5, all 6 wavetables, unison, hard
sync, 3 oscillators, 4 sub shapes, white/pink noise, all 9 filter types,
parallel routing, 4 shape modes, all 8 effects, LFO and envelope modulation,
voice stealing, and 10 factory presets on a 4-note phrase. All 59 factory
presets load and render (`ctest`, core test).

Bit-exactness on this machine comes from following the porting rules strictly
and from glibc's libm matching V8 for the functions used. Another libm could
introduce last-bit differences; the −60 dBFS threshold leaves room for that.

## Deviations

- Preset validation (`preset-io.ts` rules) is applied to every preset,
  including factory ones, which the browser loaded unvalidated. Factory data is
  already within range, so the result is identical.
- `sg-render` reads MIDI files (SMF format 0/1, tempo map) and note lists;
  the reference had no offline renderer.

## Reference bugs (matched, pending approval to fix)

None found yet.

## Licensing

Decision (Phase 1, approved by the project owner): **no JUCE.** JUCE is
AGPLv3-or-commercial, which conflicts with shipping MIT binaries.

| Piece | Uses | Licence |
|-------|------|---------|
| `engine/core`, `sg-render`, `sg-ctl` | nothing but the C++ standard library | MIT (ours) |
| `soundgineer-engine` | JACK API, provided on Omarchy by `pipewire-jack` | pipewire-jack: MIT. (jack2's libjack is LGPL-2.1, dynamically linked, also fine.) |
| VST3 / LV2 / Standalone (Phase 6) | DPF (DISTRHO Plugin Framework) | ISC |

MIDI: the engine opens a JACK MIDI input port. Under PipeWire, hardware and
ALSA-sequencer MIDI devices appear as JACK MIDI ports, so no separate ALSA
sequencer code is needed.

Nothing in the build is fetched at configure time for the engine. DPF will
be pinned by commit when Phase 6 adds it.
