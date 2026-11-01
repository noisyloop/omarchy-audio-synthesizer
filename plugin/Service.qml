import QtQuick
import Quickshell
import Quickshell.Io

// Singleton service: the only place that talks to soundgineer-engine.
// Bar widgets (one per monitor) reach it through bar.shell.serviceFor(<id>).
//
// Transport: newline-delimited JSON over $XDG_RUNTIME_DIR/soundgineer.sock
// (docs/protocol.md). The socket belongs to the systemd user unit
// soundgineer.socket, so the first connect starts the engine.
//
// Exposes the same `engine` API as demo/MockEngine.qml, consumed by
// plugin/ui/PanelBody.qml.
Item {
  id: root

  // Injected by the shell.
  property var shell: null
  property var manifest: null
  property string omarchyPath: ""

  readonly property string socketPath: Quickshell.env("XDG_RUNTIME_DIR") + "/soundgineer.sock"

  // ---- connection
  readonly property bool connected: socket.connected
  property bool setupNeeded: false
  property string setupReason: ""
  property int reconnectDelayMs: 250
  property string engineVersion: ""
  property string audioError: ""
  property string lastError: ""

  // ---- engine API (see PanelBody.qml)
  property bool powered: false
  property string patchName: ""
  property string category: ""
  property var values: ({})
  property var matrix: []
  property var presets: []
  property var favorites: ({})
  property var held: []
  property int voices: 0
  property real peakL: 0
  property real peakR: 0
  property var scope: []
  property var spectrum: []
  property int revision: 0
  property var files: ({ wavetable: [], sample: [] })

  function norm(id) { return values[id] !== undefined ? values[id] : 0 }
  function setNorm(id, n) {
    var v = Math.max(0, Math.min(1, n))
    values[id] = v
    revision++
    send({ type: "param", id: id, value: v })
  }
  function noteOn(note) {
    if (held.indexOf(note) < 0) held = held.concat([note])
    send({ type: "noteOn", note: note, velocity: 0.9 })
  }
  function noteOff(note) {
    held = held.filter(function(n) { return n !== note })
    send({ type: "noteOff", note: note })
  }
  function allNotesOff() { held = []; send({ type: "allNotesOff" }) }
  function loadPreset(name) { send({ type: "loadPreset", name: name }) }
  function toggleFavorite(name) { send({ type: "setFavorite", name: name, on: !favorites[name] }) }
  function setPower(on) { send({ type: "power", on: !!on }) }
  function togglePower() { setPower(!powered) }
  function refreshFiles() {
    send({ type: "listFiles", kind: "wavetable" })
    send({ type: "listFiles", kind: "sample" })
  }
  function importWavetable(osc, path) { send({ type: "setWavetable", osc: osc, path: path }) }
  function importSample(path) { send({ type: "setSample", path: path }) }
  function setScopeWanted(on) { send({ type: "subscribe", status: true, scope: !!on }) }

  // The UI keeps a compact slot list; the engine has 32 indexed slots.
  function sendMatrix() {
    for (var s = 0; s < 32; s++)
      send({ type: "mod", slot: s, state: s < matrix.length ? matrix[s] : null })
    revision++
  }
  function addSlot(source, dest) {
    if (matrix.length >= 32) return -1
    matrix = matrix.concat([{ source: source, dest: dest, depth: 0.5, enabled: true }])
    sendMatrix()
    return matrix.length - 1
  }
  function updateSlot(i, changes) {
    var m = matrix.slice()
    var s = Object.assign({}, m[i])
    for (var k in changes) s[k] = changes[k]
    m[i] = s
    matrix = m
    send({ type: "mod", slot: i, state: s })
    revision++
  }
  function removeSlot(i) {
    var m = matrix.slice()
    m.splice(i, 1)
    matrix = m
    sendMatrix()
  }

  // ---- transport
  function send(msg) {
    if (!socket.connected) return false
    socket.write(JSON.stringify(msg) + "\n")
    socket.flush()
    return true
  }

  function handleLine(line) {
    var text = String(line || "").trim()
    if (text === "") return
    var msg
    try { msg = JSON.parse(text) } catch (e) { return }
    if (!msg || typeof msg.type !== "string") return

    switch (msg.type) {
    case "hello":
      engineVersion = String(msg.version || "")
      break
    case "state":
      powered = !!msg.powered
      patchName = String(msg.patchName || "")
      category = String(msg.category || "")
      values = msg.params || {}
      matrix = (msg.mods || []).map(function(m) { return { source: m.source, dest: m.dest, depth: m.depth, enabled: m.enabled !== false } })
      audioError = msg.audio && msg.audio.error ? String(msg.audio.error) : ""
      held = []
      revision++
      break
    case "presetList": {
      var favs = {}
      presets = (msg.presets || []).map(function(p) {
        if (p.favorite) favs[p.name] = true
        return { name: String(p.name), category: String(p.category || "User") }
      })
      favorites = favs
      revision++
      break
    }
    case "status":
      voices = msg.voices | 0
      peakL = Number(msg.peakL) || 0
      peakR = Number(msg.peakR) || 0
      break
    case "scope":
      scope = msg.left || []
      spectrum = msg.spectrum || []
      break
    case "fileList": {
      var next = { wavetable: files.wavetable, sample: files.sample }
      next[msg.kind === "sample" ? "sample" : "wavetable"] = (msg.files || []).map(function(f) { return { name: String(f.name), path: String(f.path) } })
      files = next
      break
    }
    case "error":
      lastError = String(msg.message || "")
      break
    case "param":
      values[msg.id] = Number(msg.value) || 0
      revision++
      break
    }
  }

  Socket {
    id: socket
    path: root.socketPath
    parser: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onConnectedChanged: {
      if (connected) {
        root.reconnectDelayMs = 250
        root.setupNeeded = false
        root.send({ type: "hello", client: "omarchy-plugin", protocol: 1 })
        root.send({ type: "subscribe", status: true, scope: false })
        root.send({ type: "getState" })
        root.send({ type: "listPresets" })
      } else {
        root.voices = 0
        root.peakL = 0
        root.peakR = 0
        root.held = []
        reconnect.schedule()
      }
    }
  }

  // Backoff 0.25 s doubling to 8 s. Each attempt first checks that the unit
  // is installed, so a missing install shows the setup card instead of
  // retrying silently forever.
  Timer {
    id: reconnect
    repeat: false
    function schedule() {
      interval = root.reconnectDelayMs
      root.reconnectDelayMs = Math.min(8000, root.reconnectDelayMs * 2)
      restart()
    }
    onTriggered: if (!socket.connected && !setupCheck.running) setupCheck.running = true
  }

  Process {
    id: setupCheck
    command: ["systemctl", "--user", "cat", "soundgineer.socket"]
    onExited: function(exitCode) {
      if (exitCode !== 0) {
        root.setupNeeded = true
        root.setupReason = "The Soundgineer engine is not installed yet."
        reconnect.schedule()
        return
      }
      socket.connected = true
      if (!socket.connected) reconnect.schedule()
    }
  }

  Component.onCompleted: setupCheck.running = true
}
