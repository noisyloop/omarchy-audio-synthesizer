# Architecture

```
┌──────────────── omarchy-shell (Quickshell / QML) ─────────────────┐
│  plugin/Widget.qml  bar button + KeyboardPanel popup (per monitor) │
│        │ bar.shell.serviceFor(<plugin id>)                         │
│  plugin/Service.qml singleton ── Socket, JSON lines ────────────┐  │
└─────────────────────────────────────────────────────────────────┼──┘
                     systemd --user soundgineer.socket (0600)     ▼
                     soundgineer-engine (C++20, JUCE, headless)
                     PipeWire via JACK API · ALSA sequencer MIDI
          engine/core (no JUCE, no I/O) also feeds VST3 + LV2 + Standalone
```

## Layout

| Path | What |
|------|------|
| `manifest.json` | Omarchy plugin manifest (schema 1). Must stay at the repo root. |
| `plugin/` | QML entry points loaded by omarchy-shell. |
| `engine/core/` | DSP core: pure C++20, no JUCE, no allocation on the audio thread, no I/O. |
| `engine/app/` | `soundgineer-engine` daemon (JUCE audio devices + MIDI, socket server). |
| `engine/tools/` | `sg-render` (offline render) and `sg-ctl` (socket CLI). JUCE-free. |
| `tests/` | Unit tests; `tests/parity/` holds the reference harness and fixtures. |
| `tools/` | Vendored `omarchy-plugin-validate` and helpers. |
| `cmake/` | JUCE pin. |

## Rules that shape the layout

- The repository root *is* the installed plugin directory
  (`~/.config/omarchy/plugins/<id>/`). The shell watches it recursively and
  hot-reloads on any write, so nothing builds or writes inside it: builds go
  to `$XDG_CACHE_HOME/soundgineer/build`, and the top-level CMake refuses
  in-source builds.
- No symlinks anywhere in the tree (`omarchy plugin validate` rejects them).
- Threads in the engine: the audio callback (real-time: no locks,
  allocation or I/O); a control thread (socket, presets, wavetable builds);
  a telemetry drain at ≤ 30 Hz. Control → audio goes through a lock-free SPSC
  queue; buffers are swapped by pointer and freed off the audio thread.
