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

## Deviations

None yet.

## Reference bugs (matched, pending approval to fix)

None found yet.

## Licensing

The project's own code is MIT, with the reference copyright notice kept in
`LICENSE`. JUCE, which the build spec requires for the daemon and the
VST3/LV2/Standalone targets, is dual-licensed **AGPLv3 or commercial**
(JUCE 9 licence). Consequences:

- Source in this repository stays MIT. `engine/core/` has no JUCE code at
  all, so it and `sg-render` / `sg-ctl` are MIT in every form.
- Any binary that links JUCE (`soundgineer-engine`, the plugins, the
  Standalone app) is, when distributed, covered by AGPLv3 unless the
  distributor holds a JUCE commercial licence.
- `juce_analytics` is never linked, `JUCE_USE_CURL=0`, and CI checks the
  daemon links no network or GUI libraries.

**Open decision for the project owner.** Options: (a) accept AGPLv3 for
JUCE-linked binaries; (b) obtain a JUCE licence; (c) replace JUCE with
MIT/ISC-compatible pieces (libpipewire or the JACK API directly for the
daemon, DPF for VST3/LV2/Standalone). Because the core is JUCE-free, (c)
only touches `engine/app/` and the Phase 6 plugin wrapper.
