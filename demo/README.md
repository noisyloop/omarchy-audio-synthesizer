# Panel demo

A runnable preview of the keyboard-first Soundgineer panel, driven by a mock
engine. It needs no engine, no omarchy-shell and no network. It makes **no
sound**: the scope, spectrum, levels and voice dots are simulated.

```bash
qml demo/Main.qml          # Arch: provided by qt6-declarative (already on Omarchy)
```

What it is for: trying the layout and the keys before the real plugin UI
(Phase 5) is built. Once settled, its pages move into `plugin/` and switch from
the stand-in `Theme` singleton to the shell's `Commons.Color` / `Style` tokens.

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
| `o` | power |
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
| `Main.qml` | window, header (scope, spectrum, voices, peak), pages, keys |
| `MockEngine.qml` | fake patch state and 30 Hz telemetry |
| `Model.js` | parameter ids, ranges, curves and the 59 factory preset names, from the reference |
| `ItemRow.qml`, `Picker.qml` | row and filterable picker components |
| `Theme.qml` | stand-in for the shell theme tokens |
