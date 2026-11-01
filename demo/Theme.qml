pragma Singleton
import QtQuick

// Stand-in for omarchy-shell's qs.Commons tokens so the demo runs with plain
// `qml`. Names mirror Commons.Color / Style; the plugin itself must use the
// real tokens and never literal colors. Values approximate Omarchy's default
// (Tokyo Night) theme.
QtObject {
  readonly property color background: "#1a1b26"
  readonly property color foreground: "#a9b1d6"
  readonly property color accent: "#7aa2f7"
  readonly property color urgent: "#f7768e"

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
