import QtQuick
import Quickshell
import Quickshell.Io
import "ui"

// Singleton service: the only place in the shell that talks to
// soundgineer-engine. Bar widgets (one per monitor) reach it through
// bar.shell.serviceFor(<id>). The protocol lives in ui/EngineClient.qml; this
// file adds the Quickshell socket, reconnect with backoff, and the setup check.
//
// The socket belongs to the systemd user unit soundgineer.socket, so the first
// connect starts the engine.
EngineClient {
  id: root

  // Injected by the shell.
  property var shell: null
  property var manifest: null
  property string omarchyPath: ""

  readonly property string socketPath: Quickshell.env("XDG_RUNTIME_DIR") + "/soundgineer.sock"
  property int reconnectDelayMs: 250

  connected: socket.connected
  writeLine: function(line) { socket.write(line); socket.flush() }

  Socket {
    id: socket
    path: root.socketPath
    parser: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onConnectedChanged: {
      if (connected) {
        root.reconnectDelayMs = 250
        // After this handler, so the `connected` binding has updated.
        Qt.callLater(function() { root.startSession("omarchy-plugin", false) })
      } else {
        root.resetLive()
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
