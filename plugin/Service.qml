import QtQuick
import Quickshell
import Quickshell.Io

// Singleton service: the only place that talks to soundgineer-engine.
// Bar widgets (one per monitor) read state from here through
// bar.shell.serviceFor(<plugin id>).
//
// Transport: newline-delimited JSON over $XDG_RUNTIME_DIR/soundgineer.sock.
// The socket belongs to the systemd user unit soundgineer.socket, so the
// first connect starts the engine. See docs/protocol.md.
Item {
  id: root

  // Injected by the shell.
  property var shell: null
  property var manifest: null
  property string omarchyPath: ""

  readonly property string socketPath: Quickshell.env("XDG_RUNTIME_DIR") + "/soundgineer.sock"

  // Connection state.
  readonly property bool connected: socket.connected
  property bool setupNeeded: false
  property string setupReason: ""
  property int reconnectDelayMs: 250

  // Engine state mirrored from messages (filled in by Phase 4/5).
  property bool powered: false
  property string patchName: ""
  property int voices: 0
  property real peakL: 0
  property real peakR: 0
  property string engineVersion: ""

  signal message(var msg)

  function send(msg) {
    if (!socket.connected) return false
    socket.write(JSON.stringify(msg) + "\n")
    socket.flush()
    return true
  }

  function setPower(on) { return send({ type: "power", on: !!on }) }
  function togglePower() { return setPower(!powered) }

  function handleLine(line) {
    var text = String(line || "").trim()
    if (text === "") return
    var msg
    try { msg = JSON.parse(text) } catch (e) { return }
    if (!msg || typeof msg.type !== "string") return

    if (msg.type === "hello") {
      engineVersion = String(msg.version || "")
    } else if (msg.type === "status") {
      voices = msg.voices | 0
      peakL = Number(msg.peakL) || 0
      peakR = Number(msg.peakR) || 0
    } else if (msg.type === "state") {
      if ("powered" in msg) powered = !!msg.powered
      if ("patchName" in msg) patchName = String(msg.patchName || "")
    }
    message(msg)
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
        root.send({ type: "getState" })
      } else {
        root.voices = 0
        root.peakL = 0
        root.peakR = 0
        reconnect.schedule()
      }
    }
  }

  // Backoff 0.25 s doubling to 8 s. Every attempt first checks the unit is
  // installed, so a missing install shows the setup card instead of retrying
  // silently forever.
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
        root.setupReason = "Soundgineer's engine is not installed. Run ./install.sh in the plugin folder."
        reconnect.schedule()
        return
      }
      socket.connected = true
      // A refused connect leaves connected false without a change signal,
      // so keep the retry loop alive regardless.
      if (!socket.connected) reconnect.schedule()
    }
  }

  Component.onCompleted: setupCheck.running = true
}
