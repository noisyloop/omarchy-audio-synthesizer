import QtQuick
import Quickshell
import qs.Ui
import qs.Commons
import qs.Commons as Commons

// Bar widget + popup, following the first-party pattern: one bar-widget entry
// point that extends Ui.Panel and hosts a KeyboardPanel whose content is a
// PanelKeyCatcher. Phase 1 stub: shows connection / setup state only. The
// paged synth UI (Osc, Filter, Shape, Mod, FX, Presets) arrives in Phase 5.
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

  readonly property bool connected: !!service && service.connected
  readonly property bool setupNeeded: !service || service.setupNeeded
  readonly property string patchName: service && service.patchName !== "" ? service.patchName : "Soundgineer"

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  onOpenedChanged: if (opened)
    Qt.callLater(function() { if (panel.loadedContent) panel.loadedContent.forceActiveFocus() })

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    labelText: root.patchName
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
    focusTarget: panel.loadedContent
    contentWidth: panel.fittedContentWidth(Style.space(520))
    contentHeight: panel.fittedContentHeight(panel.loadedContentHeight, Style.space(560))

    content: PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      implicitHeight: column.implicitHeight
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Column {
        id: column
        width: parent.width
        spacing: Style.spacing.panelGap

        Text {
          width: parent.width
          text: "Soundgineer"
          textFormat: Text.PlainText
          color: Commons.Color.popups.text
          font.family: Style.font.family
          font.pixelSize: Style.font.heading
          font.bold: true
        }

        Text {
          width: parent.width
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
          color: Commons.Color.popups.text
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          text: root.setupNeeded
            ? (root.service && root.service.setupReason !== ""
                ? root.service.setupReason
                : "Waiting for the Soundgineer service to load.")
            : root.connected
              ? "Engine connected" + (root.service.engineVersion !== "" ? " (" + root.service.engineVersion + ")" : "")
              : "Connecting to the engine…"
        }

        Text {
          width: parent.width
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
          opacity: 0.7
          color: Commons.Color.popups.text
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
          text: "Esc closes · Tab switches panels · right-click the bar button toggles power"
        }
      }
    }
  }
}
