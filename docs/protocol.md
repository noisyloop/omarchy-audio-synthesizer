# Control protocol

Status: **draft** (Phase 1). Messages are specified now and implemented in
Phase 4; until then the engine does not open the socket.

## Transport

- Unix stream socket at `$XDG_RUNTIME_DIR/soundgineer.sock`, mode `0600`.
  Owned by the systemd user unit `soundgineer.socket`; connecting starts
  `soundgineer.service`. For development the engine can create the socket
  itself (`soundgineer-engine --listen`).
- One JSON object per line (UTF-8, `\n` terminated). Every object has a
  string `type`. Unknown types are answered with an `error` and otherwise
  ignored, so either side can be newer.
- No network sockets of any kind.

## Messages kept from the reference

Names and payload shapes follow the reference `messages.ts`. Parameter values
are normalized `0..1`, as in the reference.

| Direction | `type` | Payload |
|-----------|--------|---------|
| client → engine | `param` | `{ index, value }` or `{ id, value }` (id form is an addition) |
| client → engine | `noteOn` | `{ note, velocity }` |
| client → engine | `noteOff` | `{ note }` |
| client → engine | `sustain` | `{ down }` |
| client → engine | `pitchBend` | `{ value }` (-1..1) |
| client → engine | `modWheel` | `{ value }` (0..1) |
| client → engine | `aftertouch` | `{ value }` (0..1) |
| client → engine | `mod` | `{ slot, state: { source, dest, depth, enabled } \| null }` |
| client → engine | `lfoShape` | `{ lfo, points: [{ x, y, power }] }` |
| client → engine | `fxOrder` | `{ order: [int] }` |
| client → engine | `allNotesOff` | `{}` |
| engine → client | `ready` | `{}` |
| engine → client | `scope` | `{ left: [float], right: [float] }`, decimated to 256 points, 4 decimals |
| engine → client | `status` | `{ voices, peakL, peakR, sources: [float x24] }` |

The reference `wavetable` (raw mip arrays) and `sample` (raw PCM) messages are
replaced by `setWavetable` and `setSample` below: binary audio never travels
over the socket.

## Additions for an engine-owned patch

In the browser the UI owned the patch. Here the engine does, so it needs:

| Direction | `type` | Payload |
|-----------|--------|---------|
| client → engine | `hello` | `{ client, protocol }` |
| engine → client | `hello` | `{ version, protocol }` |
| client → engine | `getState` | `{}` |
| engine → client | `state` | `{ powered, patchName, params: { id: value }, mods, lfoShapes, fxOrder, wavetables }` |
| client → engine | `subscribe` | `{ status: bool, scope: bool }`; telemetry is sent only while subscribed, ≤ 30 Hz |
| client → engine | `power` | `{ on }`; off releases the audio stream and MIDI ports |
| client → engine | `setWavetable` | `{ osc, name }` or `{ osc, path }` (Serum-style WAV) |
| client → engine | `setSample` | `{ path }` or `{ path: null }` |
| client → engine | `listPresets` | `{}` |
| engine → client | `presetList` | `{ presets: [{ name, category, factory, path }] }` |
| client → engine | `loadPreset` | `{ name }` or `{ path }` |
| client → engine | `savePreset` | `{ name, category? }`; writes `$XDG_DATA_HOME/soundgineer/presets/<name>.json` |
| engine → client | `error` | `{ message, request? }` |

Every addition gets a JSON example here when it is implemented.
