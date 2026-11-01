# Security review (v0.1, first pass)

Scope: engine (socket server, file handling, audio thread), Omarchy plugin
QML, plugin builds, install scripts. Threat model: other local users and
untrusted files (presets, WAVs) opened by the user. Same-user processes are
trusted, because they can already do anything the user can.

## Controls in place

- **Socket:** `$XDG_RUNTIME_DIR/soundgineer.sock`, mode 0600, created by
  systemd (`SocketMode=0600`) or with umask 0177. `XDG_RUNTIME_DIR` itself is
  0700.
- **No network:** the service unit sets `RestrictAddressFamilies=AF_UNIX` and
  `NoNewPrivileges=yes`. No network code is linked.
- **Input limits:**
  - 8 clients, 256 KB per line, 4 MB output buffer per client
  - JSON input up to 4 MB with nesting depth 64
  - presets 128 KB, WAV files 64 MB
  - 64 LFO points, bounded slot, oscillator and LFO indices
  - every number is checked for finiteness and clamped
- **Files:**
  - `loadPreset` accepts only files inside the factory and user preset
    folders.
  - `savePreset` writes `<slug>.json` into the user preset folder only; slugs
    are `[a-z0-9-]`, so a name can't escape the folder.
  - Imports must be regular `.wav` files. Writes are atomic (temp file, then
    rename).
- **Preset contents:** validated with the reference `preset-io.ts` rules.
  Unknown ids are ignored; ranges are clamped.
- **Audio thread:** no allocation, no locks and no I/O. Commands come in
  through SPSC queues; replaced LFO shapes and samples are freed on the
  control thread.
- **Plugin QML:** runs one fixed command (`systemctl --user cat
  soundgineer.socket`, argv form, no shell). It shows engine strings as plain
  text and hardcodes no paths outside its own folder.
- **Install:** no sudo, and the script refuses to run as root. It writes only
  under `~/.cache`, `~/.local`, plus `~/.lv2` and `~/.vst3` with
  `--plugins`. `rm` targets only fixed subpaths.

## Fixed in this pass

1. **WAV header with an odd bit depth** (for example 4-bit) made
   `bytesPerSample` zero, so the engine divided by zero and crashed on import.
   Only 8, 16, 24 and 32-bit files with 1–64 channels are accepted now.
2. **Reading favorites** bound a reference to a temporary JSON value, which
   is undefined behavior. It now keeps the parsed value.
3. **VST3/LV2:** changing the wavetable parameter took the bank mutex on the
   audio thread. Table pointers are now cached at construction.
4. **Imports** were only size-checked. The engine now also requires a
   regular file with a `.wav` extension.

## Accepted / open

- **Imports can read any `.wav` the user can read.** Only same-user clients
  can connect, so this grants nothing new.
- **DPF `setState`** (mod matrix, LFO and FX state) applies directly. Hosts
  normally restore state outside `run()`; a host that interleaves the two
  could race. Revisit if a host shows glitches on session load.
- **Supply chain:** DPF is pinned by git commit, not by tarball checksum. The
  AUR build uses `git+` sources with `SKIP` checksums, which is standard for
  VCS sources.
- **No fuzzing yet.** Good next targets are `Json::parse`, `decodeWav`, the
  MIDI file reader in `sg-render`, and `Patch::fromJson`.
