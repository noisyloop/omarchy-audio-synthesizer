# Build prompt v2 — Soundgineer for Omarchy

This is the original build prompt after one OODA pass (observe the real
Omarchy `quattro` plugin contract and the Soundgineer reference source, orient
against the original spec, decide on fixes, act by rewriting). Changes against
v1 are listed in [§0](#0-what-changed-from-v1-and-why); the rest is the prompt
itself, ready to run phase by phase.

Research sources (read-only clones, never inside this repo):

- `omacom/omarchy@quattro`: `docs/omarchy-shell.md`, `manual/32-shell-plugins.md`,
  `agents/skills/shell-dev.md`, `shell/services/PluginRegistry.qml`,
  `shell/services/PluginShellApi.qml`, `shell/Ui/{Panel,KeyboardPanel,PanelKeyCatcher}.qml`,
  `shell/plugins/services/media/` and `shell/plugins/panels/audio/`,
  `bin/omarchy-plugin-validate`.
- `noisyloop/soundgineer@926f3e1`: `src/worklet/*`, `src/shared/*`,
  `src/audio/engine.ts`, `src/ui/presets.ts`, `src/ui/preset-io.ts`, `docs/`.

---

## 0. What changed from v1, and why

| # | v1 said | Reality found | v2 decision |
|---|---------|---------------|-------------|
| 1 | `kinds: ["bar-widget", "panel"]` with a `Service.qml` entry point | The shell only loads `entryPoints.service` when `kinds` contains `"service"`. The validator requires an entry point for every kind claimed (`barWidget`, `panel`, `service` keys). | `kinds: ["service", "bar-widget"]`, `entryPoints: { service, barWidget }`. |
| 2 | Separate `panel` kind for the UI, plus "follow KeyboardPanel + PanelKeyCatcher" | First-party popups (audio, network, bluetooth, agents) are **one `bar-widget` entry point** that extends `qs.Ui` `Panel` and hosts a `KeyboardPanel` popup. The `panel` kind is a free-floating summoned window (like the OSD), not a bar popup. | The panel is the bar widget's `KeyboardPanel` popup. No `panel` kind in v1. (A detached `panel` window can be added later if the popup is too small.) |
| 3 | "Service.qml starts the engine if it isn't running" | Widgets are instantiated once per monitor; the service is a singleton. Quickshell ships `Quickshell.Io.Socket` + `SplitParser` (the shell's own IPC uses them), so QML can speak newline-JSON natively. Third-party widgets reach their own service through `shell.serviceFor(manifest.id)`. | Socket client lives only in `Service.qml`. Engine start goes through **systemd socket activation** (`soundgineer.socket`, `%t/soundgineer.sock`, `SocketMode=0600`): connecting starts it, no PID juggling, permissions enforced by systemd. |
| 4 | Engine sources and build live in the plugin repo, `install.sh` builds it | The plugin checkout *is* `~/.config/omarchy/plugins/<id>/`, and the shell runs `inotifywait -m -r` over that tree and hot-reloads the plugin on any write. An in-tree JUCE build would reload the shell thousands of times. | `install.sh` builds **out of tree** in `$XDG_CACHE_HOME/soundgineer/build`. Nothing writes inside the plugin dir at runtime. `.gitignore` the usual build dirs anyway. |
| 5 | "Fresh Omarchy 4 install is the only prerequisite" + build from source | A fresh install has no guarantee of `cmake`/`base-devel`/`alsa-lib` headers, and installing them needs sudo, which we may not run. | `install.sh` checks for the toolchain and, if missing, prints the exact `sudo pacman -S --needed …` line and exits. The AUR package is the zero-toolchain path. Definition of done is adjusted accordingly. |
| 6 | Port `src/worklet` + `src/shared` | The **authoritative patch state, wavetable generation/selection, and preset (de)serialization live on the main thread** (`src/audio/engine.ts`, `src/ui/preset-io.ts`). The 59 factory presets are TS code using `P()` raw→normalized conversion inside a DOM-importing UI file. | Also port `engine.ts` patch logic and `preset-io.ts` sanitizing into `engine/core`. Factory presets are **extracted once to JSON** by a script in the reference checkout and committed as data. |
| 7 | "Seed both sides identically where possible" | Randomness is exactly three `Math.random()` call sites in `voice.ts` (per-note random, unison start-phase randomization, white/pink noise). | The golden harness replaces `Math.random` with a seeded `mulberry32`; the port uses the same generator via one injected RNG, called in the same order. Noise/random paths then become deterministic and use the **same −60 dB threshold**; spectral matching is only the fallback. |
| 8 | "−60 dBFS residual" | JS does all scalar math in `double` and only rounds to float when storing into `Float32Array`. A C++ port that uses `float` for scalar state will drift in feedback paths (SVF, comb, Freeverb, delay) far past −60 dB. | Porting rule: **JS `number` → `double`; `Float32Array` → `float`**, same order of operations, same 128-sample control block (`BLOCK = 128`) regardless of host buffer size. |
| 9 | Phase 2 port whole DSP, Phase 3 parity tests | Whole-patch diffs make it hard to find which class diverged. | Phases 2+3 merge into **one test-first loop per class**: golden vectors per class (envelope, LFO, SVF modes, comb, formant, each effect, wavetable osc) → port class → pass → next. Whole-patch and preset cases come last. Faster and localizes bugs. |
| 10 | Golden harness "stub the AudioWorkletProcessor globals" | Reference sources use extensionless TS imports, so plain Node 24 type-stripping won't resolve them. | Harness = Node 24 + a ~20-line `module.register` resolve hook (append `.ts`), stubbing `sampleRate`, `currentTime`, `registerProcessor`, `AudioWorkletProcessor`. No new npm deps. Lives in this repo under `tests/parity/harness/` and is run against a reference checkout path, so the reference repo is never modified. |
| 11 | Computer-keyboard piano (A W S E D F T G Y H U J K, Z/X) + hjkl navigation | `PanelKeyCatcher` consumes `h j k l`, `x`, Space, Enter, Tab. H/J/K and X collide with the piano map. It also has no key-release handling, which a piano needs. | Piano mode (toggle key `p`) sets `blocked: true` on the catcher and uses its own `Keys.onPressed/onReleased` handler, ignoring auto-repeat. `Esc` leaves piano mode first, closes the panel second. Closing the panel or losing focus sends `allNotesOff`. |
| 12 | Message names identical to `messages.ts` | `messages.ts` has no preset, state-query, or wavetable-by-name messages because the browser UI owns the patch. Binary payloads (`Float32Array` mips) don't belong in JSON. | Keep every `ToWorklet`/`FromWorklet` name and shape. Add engine-owned messages (`hello`, `getState`, `state`, `paramById`, `loadPreset`, `savePreset`, `listPresets`, `presetList`, `setWavetable` by name/path, `power`, `error`). Wavetables travel by **name or file path**, never as sample arrays. Params accept `index` **or** `id`. Scope samples are decimated (e.g. 256 points) and rounded to 4 decimals. |
| 13 | "Bar widget: power toggle" | Ambiguous: stop the service, or keep it but release audio? | Proposal: power **off** = engine closes its PipeWire stream and MIDI ports (0 CPU, socket stays up so the bar keeps state); power **on** reopens them. Stopping the systemd unit stays available via `sg-ctl` / `systemctl --user`. *Needs your OK.* |
| 14 | JUCE for everything | JUCE GUI modules pull X11/freetype/webkit headers into a headless daemon. | `soundgineer-engine` links only `juce_core`, `juce_events`, `juce_audio_basics`, `juce_audio_devices` (JACK backend → `pipewire-jack`, ALSA seq MIDI). Plugin/Standalone targets use `juce_audio_processors` + `juce_audio_utils`. JUCE is pinned by tag via CMake `FetchContent` **at build time only**; the AUR package pins the same tarball + checksum. |

Also noted, no change needed: `qs.Commons` palette must be imported as
`import qs.Commons as Commons` and used as `Commons.Color.*` (Qt 6.12 shadows
`Color`); animations go through `Style.duration()`; widget IPC target is
named after the plugin id; `omarchy plugin add` never runs plugin code, so the
install step is always a separate `./install.sh`.

---

## 1. Context

You are building **Soundgineer for Omarchy**, a native Linux rebuild of
Soundgineer, a wavetable synthesizer that runs in the browser. Work in this
repository. The original TypeScript project is the reference implementation
for sound and behavior. It is not a codebase to wrap, embed, or run in a webview.

Reference repo (read-only; clone next to this repo, never into it):
`https://github.com/noisyloop/soundgineer` — pin the commit you port from in
`docs/porting-notes.md`.

Read before writing code: all of `src/worklet/`, `src/shared/`, `docs/`,
`README.md`, **and** `src/audio/engine.ts`, `src/ui/preset-io.ts`,
`src/ui/presets.ts` (data only — the UI is not ported).

Reference facts the port must preserve:

- DSP in an `AudioWorkletProcessor` with a fixed 128-sample control block.
- 16 voices, oldest/released-first stealing.
- 3 wavetable oscillators (morph, up to 16 unison voices, hard sync), sub osc,
  noise osc (white / pink / WAV sample).
- 2 filters, series or parallel: TPT SVF (LP/HP/BP/notch, 12/24 dB), comb, formant.
- Shape: soft clip, hard clip, wavefolder, bitcrusher.
- 6 DAHDSR envelopes, 8 drawable LFOs (≤64 points), 4 macros, 32-slot mod matrix,
  24 mod sources in the order of `MOD_SOURCES`.
- 2048-sample frames, per-frame FFT mip pyramids, FFT spectral morphing.
- Serum-style WAV import.
- FX rack (reorderable): chorus, phaser, flanger, ping-pong delay, Freeverb,
  3-band EQ, compressor, distortion.
- Presets: `PresetData { name, version: 1, params (id → normalized), mods,
  lfoShapes, fxOrder }`; 59 factory patches.

## 2. Target architecture

```
┌──────────────── omarchy-shell (Quickshell / QML) ─────────────────┐
│  Widget.qml (bar button + KeyboardPanel popup, one per monitor)   │
│        │ shell.serviceFor(manifest.id)                            │
│  Service.qml (singleton) ── Quickshell.Io.Socket, JSON lines ──┐  │
└────────────────────────────────────────────────────────────────┼──┘
                     systemd --user soundgineer.socket (0600)    ▼
                     soundgineer-engine (C++20, JUCE, headless)
                     PipeWire via JACK API · ALSA seq MIDI in
          engine/core (no JUCE, no I/O) also feeds VST3 + LV2 + Standalone
```

### 2a. Engine (`engine/`)

- C++20, CMake ≥ 3.24, JUCE pinned by tag via `FetchContent`.
- `engine/core/`: pure C++, no JUCE, no allocation after `prepare()`, no I/O.
  Mirrors reference files 1:1: `dsp.{h,cpp}`, `voice`, `effects`, `processor`,
  `params`, `fft`, `wavetable_gen`, `messages`, plus `patch` (from
  `engine.ts`) and `preset_io` (from `preset-io.ts`).
- Port faithfully, class by class: same names, parameter IDs, ranges, curves,
  smoothing, evaluation order. **JS `number` → `double`, `Float32Array` → `float`.**
  Randomness through one injected `Rng` (mulberry32) called where
  `Math.random()` is called.
- Don't improve the sound. Reference bugs go in `docs/porting-notes.md` and
  are matched unless approved.
- Audio thread: no allocation, locks, or I/O. Control → audio via lock-free
  SPSC queue; audio → telemetry via a second SPSC/ring, drained at ≤30 Hz by a
  non-RT thread. Wavetable/sample swaps are built off-thread and handed over
  by pointer swap, old buffers freed off-thread.
- Targets: `soundgineer-engine` (daemon), `sg-render` (offline CLI),
  `sg-ctl` (socket CLI), `Soundgineer` VST3/LV2/Standalone.
- MIDI: notes, velocity, sustain (CC64), pitch bend, mod wheel (CC1),
  channel pressure, CC 20–23 → macros 1–4.

### 2b. Control protocol

- Newline-delimited JSON over `$XDG_RUNTIME_DIR/soundgineer.sock`, mode 0600,
  created by the systemd socket unit (engine also supports creating it itself
  for dev runs). No network sockets of any kind.
- Keep every `ToWorklet` / `FromWorklet` message name and payload shape.
  Additions (see §0 #12) are documented in `docs/protocol.md` with a JSON
  example each and a version field in `hello`.
- Telemetry (`scope`, `status`) ≤ 30 Hz, only while a client has subscribed
  (`{"type":"subscribe","scope":true}`), and the scope only while the panel is open.

### 2c. Omarchy plugin (repository root + `plugin/`)

`manifest.json` at the repository root:

```json
{
  "schemaVersion": 1,
  "id": "OWNER.soundgineer",
  "name": "Soundgineer",
  "version": "0.1.0",
  "author": "OWNER",
  "license": "MIT",
  "description": "Wavetable synthesizer in your bar",
  "kinds": ["service", "bar-widget"],
  "keepLoaded": true,
  "entryPoints": {
    "service": "plugin/Service.qml",
    "barWidget": "plugin/Widget.qml"
  },
  "barWidget": {
    "displayName": "Soundgineer",
    "description": "Synth power, patch, voices, level; opens the synth panel",
    "category": "Audio",
    "allowMultiple": false,
    "defaultSection": "right"
  }
}
```

- `Widget.qml` extends `qs.Ui` `Panel` (`moduleName`/`ipcTarget` = plugin id)
  with a `KeyboardPanel` whose `content:` is a `PanelKeyCatcher`. Bar face:
  power toggle, patch name, voice-activity dot, peak meter. Click or
  `omarchy-shell OWNER.soundgineer toggle` opens it.
- `Service.qml`: connects to the socket (which socket-activates the engine),
  reconnects with backoff 0.25 s → 8 s, exposes state as properties, and sets
  `setupNeeded` with a reason when the unit or binary is missing (checked via
  `systemctl --user cat soundgineer.socket` / binary path) so the widget shows
  a setup card with the exact command to run.
- Theme: only `Commons.Color.*`, `Style.*`, `Border.*` tokens. No hex colors.
- Helper paths via `Qt.resolvedUrl()` relative to the QML file, never `$OMARCHY_PATH`.
- No symlinks. No writes inside the plugin dir. Passes `omarchy plugin validate .`.
  CI runs a vendored copy of that validator script (MIT, attributed).

## 3. UI (redesigned, not ported)

Keyboard-first panel that feels native to Omarchy.

- Navigation: arrows / `hjkl` move focus; `+`/`-` adjust, `Shift` for fine;
  `Tab`/`Shift+Tab` (or `1`–`6`) switch pages; `/` searches parameters and
  presets; `Enter` opens pickers; `Esc` backs out one level.
- Pages: Osc · Filter · Shape · Mod · FX · Presets.
- Piano mode toggled with `p`: A W S E D F T G Y H U J K, octave Z/X, while
  active the catcher is `blocked` and a dedicated press/release handler drives
  notes (auto-repeat ignored). Visible "PIANO" badge. `Esc` exits piano mode.
- Mod matrix as a list (source → target, depth), assignment via picker.
- Header visuals: oscilloscope + spectrum (Canvas or `Shape`, decimated data).
  2D wavetable frame view; 3D is out of scope for v1.
- Presets grouped by category, search, favorites; user presets as JSON under
  `$XDG_DATA_HOME/soundgineer/presets/`, favorites in
  `$XDG_STATE_HOME/soundgineer/favorites.json`. Factory presets are read-only.

[MY UI NOTES: layout sketches, references, and must-haves go here]

## 4. Build order (stop and report after each phase)

1. **Scaffold** — CMake + JUCE skeleton that builds an empty `engine/core`
   test binary; `manifest.json` + stub QML that passes validate; docs
   skeleton; CI (engine build + ctest + validator + `qmllint` if available).
2. **Reference harness + data extraction** — `tests/parity/harness/` (Node 24,
   resolve hook, globals stub, seeded `Math.random`) that can (a) drive single
   reference classes with scripted inputs and (b) render whole patches + MIDI
   to WAV. Extract the 59 factory presets and built-in wavetables to JSON/
   binary fixtures. Commit fixtures with the reference commit hash.
3. **Test-first DSP port**, in this order, each class passing before the next:
   `fft`, `wavetable_gen` (mips), `Envelope`, `Lfo`, `VoiceFilter` (every
   mode), noise/pink, wavetable osc + unison + sync, `Voice`, each effect,
   `processor` (voice manager, global LFOs, FX order). Then `sg-render` and
   the whole-patch suite: init patch at C3/C4/C5, each osc type, each filter
   mode, each effect, unison, and 10 factory presets on a short MIDI phrase.
   Threshold −60 dBFS residual for every case (RNG is seeded); spectral match
   only for a case you explicitly justify. All pass or I sign off per exception.
4. **Realtime engine** — JACK/PipeWire audio, ALSA seq MIDI, socket server,
   preset load/save, systemd units, `sg-ctl`.
5. **Omarchy plugin UI** — service, widget, panel pages, theming, piano mode.
6. **VST3 / LV2 / Standalone** from the same core; smoke-test in Carla.
7. **Packaging** — `install.sh` (toolchain check, out-of-tree build in
   `$XDG_CACHE_HOME`, installs to `~/.local/bin`, `~/.local/share/soundgineer`,
   `~/.config/systemd/user`; no sudo), `uninstall.sh` (keeps user presets),
   `PKGBUILD`.

## 5. Hard constraints

- MIT, keep the reference copyright notice alongside ours.
- No telemetry, analytics, crash reporting, update checks, or network calls at
  runtime. (JUCE is fetched at build time only.)
- No secrets or personal info in commits, docs, or presets.
- Never run plugin code at install time, never require sudo, never write
  outside `~/.config/omarchy/plugins/<id>/` (git only), `~/.local`,
  `~/.config/systemd/user`, and XDG data/state/cache/runtime dirs.
- Dependencies: JUCE, `pipewire-jack`/JACK, `alsa-lib`, Qt/Quickshell
  (provided by Omarchy). Ask before adding anything else.
- Real-time safety is a correctness requirement.

## 6. Definition of done (v1)

- With the toolchain present (or via AUR): `omarchy plugin add <repo> --enable`
  then `./install.sh` gives a working synth in the bar.
- All parity tests pass; all 59 factory presets load and match the reference.
- MIDI keyboard and computer-keyboard piano both work.
- VST3 loads in a host; LV2 loads in Ardour or Carla.
- 16 voices × 4-voice unison on all three oscillators < 15 % of one core on a
  mid-range laptop — report the measured figure and the machine.
- README covers install, usage, keyboard shortcuts, architecture, building.

## 7. Working agreement

- Before Phase 1: a short plan listing files to create; wait for OK.
- Commit at the end of each phase; report what was built, test results, and
  every deviation with its reason.
- When reference behavior is ambiguous, say so and propose an option.
