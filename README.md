# Soundgineer for Omarchy

A wavetable synthesizer that lives in the Omarchy bar. A native C++ engine
does the sound; an Omarchy shell plugin gives you a keyboard-first panel
for it.

> **Status: in development (Phase 1 of 7, scaffold).** Nothing makes sound
> yet. See [`docs/build-prompt.md`](docs/build-prompt.md) for the plan.

## Try the panel UI now

`qml demo/Main.qml` runs a preview of the keyboard-first panel against a mock
engine (no sound). See [`demo/README.md`](demo/README.md).

## Install

_Arrives in Phase 7._ The intended flow:

```bash
omarchy plugin add <this-repo-url> --enable
cd ~/.config/omarchy/plugins/<id> && ./install.sh   # builds the engine, no sudo
```

## Building from source (developers)

Needs CMake ≥ 3.24, a C++20 compiler, Ninja, and the JACK API headers
(provided on Arch by `pipewire-jack`, which is also what the engine uses at
runtime). On Arch: `sudo pacman -S --needed base-devel cmake ninja pipewire-jack`.

```bash
cmake -S . -B ~/.cache/soundgineer/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ~/.cache/soundgineer/build
ctest --test-dir ~/.cache/soundgineer/build --output-on-failure
tools/omarchy-plugin-validate .
```

Build outside the repository: when it is installed as an Omarchy plugin,
the shell reloads the plugin on every file written inside it. CMake refuses
in-source builds for that reason.

The build downloads nothing: the engine depends only on the JACK API.

## Architecture

See [`docs/architecture.md`](docs/architecture.md). In short: `engine/core`
is a dependency-free DSP core ported class by class from the TypeScript
reference; `soundgineer-engine` runs it headless on PipeWire with MIDI in;
`plugin/` is the QML bar widget and service that talk to it over a private
Unix socket ([`docs/protocol.md`](docs/protocol.md)).

## Privacy

No telemetry, analytics, crash reporting, update checks, or network access
of any kind at runtime.

## License

MIT, see [`LICENSE`](LICENSE). Every dependency is MIT-compatible; see
[`docs/porting-notes.md`](docs/porting-notes.md#licensing).
