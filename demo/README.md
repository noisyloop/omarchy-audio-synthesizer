# Panel demo

A runnable preview of the keyboard-first Soundgineer panel, driven by a mock
engine. It needs no engine, no omarchy-shell and no network. It makes **no
sound**: the scope, spectrum, levels and voice dots are simulated.

```bash
qml demo/Main.qml          # Arch: provided by qt6-declarative (already on Omarchy)
```

The panel itself is `plugin/ui/PanelBody.qml`, the exact file the Omarchy
plugin shows in its bar popup. The demo only swaps in a stand-in theme and a
mock engine, so what you see here is what the plugin shows.

## Keys

| Keys | Action |
|------|--------|
| `h j k l` / arrows | move between sections / rows |
| `+` / `-` | adjust value; `_` for a fine step down (Shift+`-`) |
| `Enter` / `Space` | toggle on/off and choices, load preset, assign mod slot |
| `r` | reset parameter to its default |
| `Tab` / `Shift+Tab` / `1`–`6` | pages: Osc · Filter · Shape · Mod · FX · Presets |
| `/` | search parameters and presets (type to filter, arrows, Enter) |
| `p` | piano mode: `A W S E D F T G Y H U J K` play, `Z`/`X` octave, `P`/`Esc` leave |
| `Shift+O` | power |
| Mod page | `Enter` pick source → target, `e` slot on/off, `x` delete |
| Presets page | `Enter` load, `f` favorite |

Piano mode takes over the letter keys on purpose: `h j k` and `x` are
navigation keys otherwise.

## Screenshots

```bash
QT_QPA_PLATFORM=offscreen qml demo/Main.qml -- --screenshot out.png \
  [--page 1-6] [--preset NAME] [--notes 60,64,67] [--cursor SECTION,ROW] [--piano] [--search TEXT]
```

## Files

| File | Role |
|------|------|
| `Main.qml` | window, screenshot flags; hosts `plugin/ui/PanelBody.qml` |
| `MockEngine.qml` | fake patch state and 30 Hz telemetry (same API as `plugin/Service.qml`) |
| `Theme.qml` | stand-in for the shell theme tokens |
| `../plugin/ui/` | the shared panel: `PanelBody.qml`, `ItemRow.qml`, `Picker.qml`, `Model.js` |
