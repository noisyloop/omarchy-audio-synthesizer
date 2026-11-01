# Parity tests

Goal: the native engine sounds identical to the TypeScript reference.

Layout (filled in from Phase 2):

```
harness/       Node 24 scripts that drive the reference DSP offline
  resolve.mjs    module.register hook: resolves extensionless .ts imports
  globals.mjs    stubs sampleRate, currentTime, registerProcessor, AudioWorkletProcessor
  seed.mjs       replaces Math.random with seeded mulberry32 (sg::Rng twin)
  unit.mjs       per-class golden vectors (Envelope, Lfo, VoiceFilter, effects, ...)
  render.mjs     whole-patch + MIDI phrase renders to WAV
  extract.mjs    factory presets and built-in wavetables to JSON fixtures
fixtures/      generated goldens, each tagged with the reference commit
compare/       C++ test that runs sg-render / core classes and reports
               RMS residual (dBFS) and spectral difference per case
```

The harness reads a local reference checkout passed by path
(`SG_REFERENCE_DIR`) and never modifies it.

Pass threshold: residual ≤ −60 dBFS for every case. Randomness is seeded on
both sides, so noise paths use the same threshold; any spectral-only case
needs an explicit, approved exception listed here.
