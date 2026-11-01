# Control protocol

Newline-delimited JSON over a Unix stream socket. Implemented by
`soundgineer-engine` (engine/app/server.cpp); `sg-ctl` and `plugin/Service.qml`
are clients.

## Transport

- Socket: `$XDG_RUNTIME_DIR/soundgineer.sock`, mode `0600`. Normally created
  by the systemd user unit `soundgineer.socket`, so the first connection
  starts `soundgineer.service`. Run by hand, the engine creates it itself
  (`--socket PATH` to override).
- One JSON object per line, each with a string `type`. Unknown types get an
  `error` reply. At most 8 clients; lines over 256 KB drop the client.
- No network sockets of any kind (the service unit restricts the engine to
  `AF_UNIX`).

## Messages kept from the reference

Names and payloads follow the reference `messages.ts`. Parameter values are
normalized `0..1`. Anywhere an index is accepted, a string id works too.

| Direction | `type` | Payload |
|-----------|--------|---------|
| → engine | `param` | `{ "id": "filter1.cutoff", "value": 0.5 }` or `{ "index": 42, "value": 0.5 }` |
| → engine | `noteOn` | `{ "note": 60, "velocity": 1 }` |
| → engine | `noteOff` | `{ "note": 60 }` |
| → engine | `sustain` | `{ "down": true }` |
| → engine | `pitchBend` | `{ "value": -1..1 }` |
| → engine | `modWheel` / `aftertouch` | `{ "value": 0..1 }` |
| → engine | `mod` | `{ "slot": 0, "state": { "source": "lfo1", "dest": "osc1.morph", "depth": 0.5, "enabled": true } }`, `state: null` clears |
| → engine | `lfoShape` | `{ "lfo": 0, "points": [{ "x": 0, "y": 0, "power": 0 }, ...] }` (≤ 64 points) |
| → engine | `fxOrder` | `{ "order": ["chorus", "phaser", "flanger", "delay", "reverb", "eq", "comp", "fxdist"] }` or indices |
| → engine | `allNotesOff` | `{}` |
| engine → | `status` | `{ "voices": 2, "peakL": 0.41, "peakR": 0.39, "sources": [24 floats] }` |
| engine → | `scope` | `{ "left": [256], "right": [256], "spectrum": [48 bins, 0..1 over −80..0 dB] }` |

Telemetry is sent at ≤ 30 Hz and only to clients that subscribed. Values are
rounded to 4 decimals. The reference's binary `wavetable` / `sample`
messages are replaced by `setWavetable` / `setSample` with file paths.

## Engine-owned patch and presets

| Direction | `type` | Payload |
|-----------|--------|---------|
| → engine | `hello` | `{ "client": "omarchy-plugin", "protocol": 1 }` |
| engine → | `hello` | `{ "version": "0.1.0", "protocol": 1 }` |
| → engine | `getState` | `{}` |
| engine → | `state` | `{ "powered", "audio": { "running", "sampleRate", "backend", "error"? }, "patchName", "category", "params": { id: value }, "mods": [...], "lfoShapes": [[...] x8], "fxOrder": [ids] }` |
| → engine | `subscribe` | `{ "status": true, "scope": true }` |
| → engine | `power` | `{ "on": false }`: releases the PipeWire stream and MIDI port; the socket stays up. Replies with `state` to everyone. |
| → engine | `setWavetable` | `{ "osc": 0, "name": "Vocal" }` or `{ "osc": 0, "path": "/path/table.wav" }` (Serum-style WAV → Custom slot) |
| → engine | `setSample` | `{ "path": "/path/sample.wav" }` or `{ "path": null }` |
| → engine | `listPresets` | `{}` |
| engine → | `presetList` | `{ "presets": [{ "name", "category", "factory", "favorite", "path" }] }` |
| → engine | `loadPreset` | `{ "name": "Kalimba" }`; replies with `state` to everyone |
| → engine | `savePreset` | `{ "name": "My Lead", "category": "Leads" }` → `$XDG_DATA_HOME/soundgineer/presets/my-lead.json` |
| engine → | `saved` | `{ "path": "..." }` |
| → engine | `setFavorite` | `{ "name": "Kalimba", "on": true }` → `$XDG_STATE_HOME/soundgineer/favorites.json` |
| engine → | `param` | `{ "id": "macro1.value", "value": 0.5 }` when MIDI CC 20–23 moves a macro |
| engine → | `error` | `{ "message": "..." }` |

The current patch is saved to `$XDG_STATE_HOME/soundgineer/session.json` on
preset load and on exit, and restored at startup.

## Example

```console
$ sg-ctl load "Kalimba"
loaded Kalimba
$ sg-ctl send '{"type":"param","id":"filter1.cutoff","value":0.4}'
$ sg-ctl note 60 0.9 0.5
$ sg-ctl watch 1
{"type":"status","voices":1,"peakL":0.2129,"peakR":0.2084,"sources":[...]}
```
