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

## Layout

```
cases.json          test cases (patch overrides or a factory preset + notes)
harness/hooks.mjs   Node load hooks: resolve extensionless .ts, expose FACTORY
harness/generate.mjs  stubs the AudioWorklet globals, seeds Math.random with
                    mulberry32 (twin of sg::Rng), mirrors engine.ts loadPreset
                    + syncAll, renders every case; also writes the factory
                    presets (presets/factory/) and the C++ parameter table
fixtures/*.wav      golden renders (float32 stereo, 44.1 kHz)
fixtures/REFERENCE  reference commit the fixtures came from
```

Run the comparison (also part of `ctest`):

```bash
SG_FACTORY_PRESETS=presets/factory sg-render --parity tests/parity/cases.json tests/parity/fixtures
```

Regenerate fixtures (only when the reference or cases change):

```bash
cd tests/parity/harness
SG_REFERENCE_DIR=/path/to/reference node --experimental-transform-types --import ./register.mjs generate.mjs
```

The harness reads a local reference checkout passed by path and never
modifies it.

Pass threshold: residual ≤ −60 dBFS for every case. Randomness is seeded on
both sides, so noise paths use the same threshold; any spectral-only case
needs an explicit, approved exception listed here.
