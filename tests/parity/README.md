# Parity tests

Goal: the native engine sounds identical to the TypeScript reference.

## Why Node, and when it runs

Node is a **developer-only** tool used to produce test fixtures. It is not
part of the build, the install, the engine or the plugin, and none of those
need it or the network.

The reason it exists at all: the reference's sound only exists as
TypeScript code. To prove the C++ port matches, something has to run that
code once and record what it outputs (golden WAVs and per-class vectors).
Node runs the reference DSP files as they are, with no browser. The outputs
are committed to `fixtures/`; after that, `ctest` compares the C++ engine
against those files offline, with no Node involved. Node is only needed
again if the reference changes and the fixtures must be regenerated.

Factory presets are not JSON in the reference: they are TypeScript objects
written in raw units (Hz, seconds) and converted with the reference's
curve functions. The same one-off script dumps them to JSON with the exact
normalized values, so the engine and the plugin load plain JSON files.

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
