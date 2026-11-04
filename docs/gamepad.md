# Game controller (sg-pad)

`sg-pad` turns a game controller into a Soundgineer controller. It reads the
pad through Linux evdev and talks to the engine over its control socket
(`docs/protocol.md`), the same way `sg-ctl` and the panel do. Nothing is
modified on the controller and no MIDI port is involved: the stock pad is
already an input device, so the bridge only has to decide what each stick,
trigger and button means.

Works with anything the kernel exposes as a gamepad: DualSense (PS5),
DualShock 4, Xbox (xpad / xone / Bluetooth), 8BitDo, Switch Pro. USB or
Bluetooth. systemd-logind gives the logged-in user access to joystick
devices, so it runs without root and without the `input` group.

```bash
sg-pad --list           # which controllers it can see
sg-pad                  # first controller found, engine socket from $XDG_RUNTIME_DIR
sg-pad --dry-run        # print the messages instead of sending them
systemctl --user enable --now soundgineer-pad   # start with your session
```

It waits for the controller and for the engine, and reconnects to either if
it goes away. Unplugging the pad sends all-notes-off.

## Default layout

| Control | Does | How |
|---------|------|-----|
| Left stick X / Y | macro 1 / macro 2 | **rel**: push to turn, the knob stays where you leave it |
| Right stick X | pitch bend | **abs**: springs back to centre |
| Right stick up | mod wheel | **abs**, up half only |
| R2 | filter 1 cutoff | **offset**: opens from the patch's value, returns on release |
| L2 | shape drive | **offset** (clipping on demand) |
| ✕ ○ □ △ R1 (A B X Y RB) | notes: root, ♭3, 4, 5, ♭7 (minor pentatonic from C3) | with velocity 0.9 |
| L1 | sustain | hold |
| D-pad up / down | octave ± | ±3 |
| D-pad left / right | previous / next preset | |
| L3 / R3 | transpose −1 / +1 semitone | ±11 |
| Options / Start | all notes off | |

The three axis modes are what make a spring-loaded stick useful as a knob:
**rel** integrates the stick (endless encoder), **abs** follows it (bend,
wobble), **offset** adds to whatever the preset already has, so a trigger
sweep never jumps the sound to an unrelated value.

## Remapping

```bash
mkdir -p ~/.config/soundgineer
sg-pad --print-config > ~/.config/soundgineer/pad.json
```

Edit and restart `sg-pad`. Any key you leave out keeps its default; `axes` and
`buttons`, if present, replace the default lists.

```json
{
  "root": 45,
  "scale": [0, 2, 3, 5, 7, 8, 10],
  "lightParam": "macro2.value",
  "axes": [
    { "axis": "lx", "target": "osc1.morph", "mode": "rel", "speed": 0.5 },
    { "axis": "ly", "target": "reverb.mix", "mode": "rel" },
    { "axis": "rx", "target": "pitchBend" },
    { "axis": "ry", "target": "filter1.resonance", "mode": "offset", "amount": 0.4 },
    { "axis": "r2", "target": "filter1.cutoff", "mode": "offset", "amount": 0.6 },
    { "axis": "l2", "target": "delay.feedback", "mode": "offset", "amount": 0.5 }
  ],
  "buttons": [
    { "button": "south", "degree": 0 }, { "button": "east", "degree": 2 },
    { "button": "west", "degree": 4 },  { "button": "north", "degree": 7 },
    { "button": "l1", "action": "sustain" },
    { "button": "up", "action": "octaveUp" }, { "button": "down", "action": "octaveDown" },
    { "button": "right", "action": "nextPreset" }, { "button": "left", "action": "prevPreset" },
    { "button": "start", "action": "panic" }
  ]
}
```

- **axes:** `lx ly rx ry` (−1..1, up is +) and `l2 r2` (0..1). `target` is any
  parameter id (`sg-ctl state` lists them) or `pitchBend`, `modWheel`,
  `aftertouch`. `mode` is `abs`, `rel` (with `speed`, full range per
  1/`speed` s) or `offset` (with `amount`). `invert` and `positiveOnly`
  are optional.
- **buttons:** `south east west north l1 r1 l3 r3 select start mode up down
  left right`. Either a scale `degree` (past the end of the scale wraps up an
  octave) or an `action`: `octaveUp octaveDown transposeUp transposeDown
  nextPreset prevPreset sustain panic`.
- `deadzone` (0.10), `velocity`, `rumble`, `lightbar`.

Macros only do something when the preset routes them in its mod matrix; pick
real parameter ids for presets that don't.

## Lightbar

The DualSense and DS4 lightbar is an LED the kernel lets only root write. To
let `sg-pad` color it (hue from `lightParam`, brightness from the output
level), install the rule once:

```bash
sudo install -m 0644 packaging/udev/70-soundgineer-pad.rules /etc/udev/rules.d/
sudo udevadm control --reload   # then replug the controller
```

It only makes that LED's color and brightness files writable. Without it,
everything else works and `sg-pad` says once that the lightbar is skipped.

## Using a pad with Ableton or other DAWs

Same idea, different plumbing: the DAW wants MIDI, so on macOS or Windows
you run a gamepad-to-MIDI app that publishes a virtual MIDI port (IAC
Driver on a Mac, loopMIDI on Windows), then MIDI-map the knobs you want in
the DAW. The controller itself is never modified. `sg-pad` skips the MIDI
step because it can speak the engine's protocol directly, which also means
it can reach every parameter, not only the four macro CCs.
