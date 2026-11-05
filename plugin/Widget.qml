import QtQuick
import Quickshell
import qs.Ui
import qs.Commons
import qs.Commons as Commons
import "ui"

// Bar widget + popup, following the first-party pattern: one bar-widget entry
// point that extends Ui.Panel and hosts a KeyboardPanel whose content is a
// PanelKeyCatcher. The catcher stays `blocked` and the synth panel
// (ui/PanelBody.qml) handles its own keys, because the piano and page keys
// overlap the catcher's hjkl / x / Tab bindings.
Panel {
  id: root

  // Injected by the shell.
  property var manifest: null
  property string omarchyPath: ""

  readonly property string pluginId: manifest && manifest.id ? String(manifest.id) : moduleName
  ipcTarget: pluginId

  // The singleton service, reached through the scoped shell facade that the
  // built-in bar hands to third-party widgets.
  readonly property var service: bar && bar.shell && typeof bar.shell.serviceFor === "function"
    ? bar.shell.serviceFor(pluginId) : null
  readonly property bool ready: !!service && service.connected
  readonly property string patchName: ready && service.patchName !== "" ? service.patchName : "Soundgineer"
  readonly property bool powered: ready && service.powered

  // Shell theme tokens mapped onto the names PanelBody expects.
  QtObject {
    id: shellTheme
    readonly property color background: Commons.Color.popups.background
    readonly property color foreground: Commons.Color.popups.text
    readonly property color accent: Commons.Color.accent
    readonly property color urgent: Commons.Color.urgent
    readonly property color popupBackground: Commons.Color.popups.background
    readonly property color popupText: Commons.Color.popups.text
    readonly property color popupBorder: Commons.Color.popups.border
    readonly property color dim: Qt.alpha(Commons.Color.popups.text, 0.55)
    readonly property color faint: Qt.alpha(Commons.Color.popups.text, 0.12)
    readonly property color cursorFill: Qt.alpha(Commons.Color.accent, 0.18)
    readonly property string fontFamily: Style.font.family
    readonly property int caption: Style.font.caption
    readonly property int body: Style.font.body
    readonly property int title: Style.font.title
    readonly property int heading: Style.font.heading
    readonly property int gap: Style.space(8)
    readonly property int padding: Style.spacing.popupPadding
    readonly property int rowHeight: Style.space(20)
    readonly property int sectionWidth: Style.space(160)
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  onOpenedChanged: {
    if (service) service.setScopeWanted(opened)
    if (opened) {
      Qt.callLater(function() {
        if (keyCatcher.body) keyCatcher.body.forceActiveFocus()
      })
    } else if (keyCatcher.body) {
      keyCatcher.body.reset()   // release piano notes
    }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    // Omarchy 4's bar buttons are icon-only (no labelText), so the patch name
    // and power state are in the tooltip.
    tooltipText: root.ready ? root.patchName + (root.powered ? "" : " (off)") + "\nClick: open · Right-click: power" : "Soundgineer (engine not running)"
    iconComponent: Component {
      // Power ring + voice dot + stereo peak bars.
      Item {
        readonly property color on: Commons.Color.accent
        readonly property color off: Qt.darker(root.barForeground, 1.6)
        Rectangle {
          id: ring
          width: button.opticalSize * 0.62
          height: width
          radius: width / 2
          anchors.verticalCenter: parent.verticalCenter
          x: (parent.width - width - bars.width - 3) / 2
          color: root.ready && root.service.voices > 0 ? parent.on : "transparent"
          border.width: Math.max(1, Math.round(button.opticalSize / 9))
          border.color: root.powered ? parent.on : parent.off
        }
        Row {
          id: bars
          anchors.left: ring.right
          anchors.leftMargin: 3
          anchors.bottom: ring.bottom
          spacing: 1
          Repeater {
            model: 2
            Rectangle {
              required property int index
              width: Math.max(2, Math.round(button.opticalSize / 7))
              height: Math.max(1, ring.height * Math.min(1, root.ready ? (index === 0 ? root.service.peakL : root.service.peakR) : 0))
              anchors.bottom: parent.bottom
              color: Commons.Color.accent
            }
          }
        }
      }
    }
    onPressed: function(buttonCode) {
      if (buttonCode === Qt.RightButton && root.service) root.service.togglePower()
      else root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher.body ? keyCatcher.body : keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(860))
    contentHeight: panel.fittedContentHeight(Style.space(600), Style.space(600))

    // Omarchy 4: the panel takes its content as children (no `content:`).
    PanelKeyCatcher {
      id: keyCatcher
      property alias body: bodyLoader.item
      anchors.fill: parent
      implicitHeight: Style.space(600)
      blocked: true

      Loader {
        id: bodyLoader
        anchors.fill: parent
        active: root.ready
        sourceComponent: PanelBody {
          theme: shellTheme
          engine: root.service
          focus: true
          onCloseRequested: root.close()
        }
      }

      // Setup / connecting card.
      Column {
        visible: !root.ready
        anchors.centerIn: parent
        width: parent.width * 0.8
        spacing: Style.spacing.panelGap

        Text {
          width: parent.width
          horizontalAlignment: Text.AlignHCenter
          text: "Soundgineer"
          color: Commons.Color.popups.text
          font.family: Style.font.family
          font.pixelSize: Style.font.heading
          font.bold: true
        }
        Text {
          width: parent.width
          horizontalAlignment: Text.AlignHCenter
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
          color: Commons.Color.popups.text
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          text: root.service && root.service.setupNeeded
            ? root.service.setupReason + "\nRun this once in a terminal:"
            : "Connecting to the engine…"
        }
        Rectangle {
          visible: !!root.service && root.service.setupNeeded
          width: parent.width
          height: cmd.implicitHeight + Style.spacing.rowPaddingX
          radius: Style.cornerRadius
          color: Qt.alpha(Commons.Color.popups.text, 0.08)
          Text {
            id: cmd
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: "cd ~/.config/omarchy/plugins/" + root.pluginId + " && ./install.sh"
            color: Commons.Color.accent
            font.family: Style.font.family
            font.pixelSize: Style.font.body
          }
        }
      }
    }
  }
}
