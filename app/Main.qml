import QtQuick
import QtQuick.Window
import Soundgineer
import "../plugin/ui"

// Standalone Soundgineer window: the bar panel (PanelBody) in a normal
// window, driven by the same EngineClient the bar uses.
Window {
  id: win
  width: 900
  height: 640
  minimumWidth: 640
  minimumHeight: 480
  visible: true
  title: client.connected && client.patchName !== "" ? "Soundgineer: " + client.patchName : "Soundgineer"
  color: theme.background

  // Omarchy theme -> the names PanelBody expects.
  QtObject {
    id: theme
    readonly property var c: omarchyTheme.colors
    readonly property color background: c.background
    readonly property color foreground: c.foreground
    readonly property color accent: c.accent
    readonly property color urgent: c.urgent
    readonly property color popupBackground: background
    readonly property color popupText: foreground
    readonly property color popupBorder: accent
    readonly property color dim: Qt.alpha(foreground, 0.55)
    readonly property color faint: Qt.alpha(foreground, 0.12)
    readonly property color cursorFill: Qt.alpha(accent, 0.18)
    readonly property string fontFamily: "monospace"
    readonly property int caption: 10
    readonly property int body: 12
    readonly property int title: 14
    readonly property int heading: 16
    readonly property int gap: 8
    readonly property int padding: 14
    readonly property int rowHeight: 20
    readonly property int sectionWidth: 160
  }

  LineSocket {
    id: sock
    onLineReceived: function(line) { client.handleLine(line) }
    onConnectedChanged: {
      if (connected) { retry.interval = 250; Qt.callLater(function() { client.startSession("soundgineer-window", true) }) }
      else { client.resetLive(); retry.start() }
    }
  }

  EngineClient {
    id: client
    connected: sock.connected
    writeLine: function(line) { sock.writeLine(line) }
  }

  // Connecting starts the engine through soundgineer.socket; retry with backoff.
  Timer {
    id: retry
    interval: 250
    running: true
    repeat: true
    onTriggered: {
      if (sock.connected) { stop(); return }
      sock.open()
      interval = Math.min(8000, interval * 2)
    }
  }

  PanelBody {
    id: panel
    anchors.fill: parent
    anchors.margins: theme.padding
    visible: client.connected
    theme: theme
    engine: client
    focus: true
    onCloseRequested: flash("Ctrl+Q quits")
  }

  Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
  onActiveChanged: if (!active && client.connected) panel.reset()   // release piano notes on focus loss

  Column {
    visible: !client.connected
    anchors.centerIn: parent
    width: parent.width * 0.7
    spacing: 12
    Text {
      width: parent.width
      horizontalAlignment: Text.AlignHCenter
      text: "Soundgineer"
      color: theme.foreground
      font.family: theme.fontFamily
      font.pixelSize: theme.heading
      font.bold: true
    }
    Text {
      width: parent.width
      horizontalAlignment: Text.AlignHCenter
      wrapMode: Text.Wrap
      textFormat: Text.PlainText
      color: theme.dim
      font.family: theme.fontFamily
      font.pixelSize: theme.body
      text: "Waiting for the engine at " + sock.path + "\nIf this persists, run ./install.sh in the plugin folder, or start soundgineer-engine by hand."
    }
  }

  Component.onCompleted: { sock.open(); panel.forceActiveFocus() }
}
