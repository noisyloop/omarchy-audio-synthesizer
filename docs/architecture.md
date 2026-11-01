# Architecture

```
┌──────────────── omarchy-shell (Quickshell / QML) ─────────────────┐
│  plugin/Widget.qml  bar button + KeyboardPanel popup (per monitor) │
│        │ bar.shell.serviceFor(<plugin id>)                         │
│  plugin/Service.qml singleton ── Socket, JSON lines ────────────┐  │
└─────────────────────────────────────────────────────────────────┼──┘
                     systemd --user soundgineer.socket (0600)     ▼
                     soundgineer-engine (C++20, headless)
                     PipeWire via JACK API (pipewire-jack) · JACK MIDI in
   engine/core (no deps, no I/O) also feeds VST3 + LV2 + Standalone (DPF)
```

## Layout

| Path | What |
|------|------|
| `manifest.json` | Omarchy plugin manifest (schema 1). Must stay at the repo root. |
| `plugin/` | QML entry points loaded by omarchy-shell (`Service.qml`, `Widget.qml`). |
| `plugin/ui/` | The panel (`PanelBody.qml` + components), shared with `demo/`. Imports no shell modules; gets `theme` and `engine` objects. |
| `engine/core/` | DSP core: pure C++20, no third-party code, no allocation on the audio thread, no I/O. Ported 1:1 from the reference; `params_table.inc` is generated from it. |
| `engine/app/` | `soundgineer-engine` daemon (JACK API audio + MIDI, socket server, presets). |
| `engine/tools/` | `sg-render` (offline render, parity, bench) and `sg-ctl` (socket CLI). |
| `engine/plugins/` | DPF wrapper: VST3, LV2, JACK standalone. |
| `presets/factory/` | 59 factory presets as JSON (extracted from the reference). |
| `packaging/` | systemd user units, AUR `PKGBUILD`. |
| `install.sh`, `uninstall.sh` | per-user build and install, no sudo. |
| `tests/` | Unit tests; `tests/parity/` holds the reference harness and fixtures. |
| `demo/` | Runnable panel preview with a mock engine (`qml demo/Main.qml`). |
| `cmake/` | DPF pin (plugin targets only). |
| `tools/` | Vendored `omarchy-plugin-validate` and helpers. |

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
