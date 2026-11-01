import QtQuick
import QtQuick.Window
import "../plugin/ui"

// Soundgineer panel demo. Runs with plain `qml`, no omarchy-shell, no engine:
//
//   qml demo/Main.qml
//
// The panel itself is plugin/ui/PanelBody.qml, the same file the Omarchy
// plugin uses; here it gets a stand-in theme and a mock engine (no sound).
Window {
  id: win
  width: 900
  height: 640
  visible: true
  title: "Soundgineer panel demo"
  color: Theme.background

  MockEngine { id: engine }

  Rectangle {
    id: card
    anchors.fill: parent
    anchors.margins: 10
    radius: 8
    color: Theme.popupBackground
    border.color: Theme.popupBorder
    border.width: 2

    PanelBody {
      id: panel
      anchors.fill: parent
      anchors.margins: Theme.padding
      theme: Theme
      engine: engine
      focus: true
      onCloseRequested: panel.flash("Esc closes the panel in Omarchy")
    }
  }

  // ------------------------------------------------------------ screenshots
  //   qml demo/Main.qml -- --screenshot out.png [--page 1-6] [--preset NAME]
  //       [--notes 60,64,67] [--cursor SECTION,ROW] [--piano] [--search TEXT]
  function arg(name) {
    var a = Qt.application.arguments
    var i = a.indexOf(name)
    return i >= 0 && i + 1 < a.length ? a[i + 1] : ""
  }
  Component.onCompleted: {
    panel.forceActiveFocus()
    var shot = arg("--screenshot")
    if (shot === "") return
    if (arg("--page") !== "") panel.goPage(Number(arg("--page")) - 1)
    if (arg("--preset") !== "") engine.loadPreset(arg("--preset"))
    if (arg("--cursor") !== "") { var c = arg("--cursor").split(","); panel.sec = Number(c[0]); panel.idx = Number(c[1]) }
    if (Qt.application.arguments.indexOf("--piano") >= 0) panel.piano = true
    if (arg("--notes") !== "") arg("--notes").split(",").forEach(function(n) { engine.noteOn(Number(n)) })
    if (arg("--search") !== "") { panel.openSearch(); panel.picker.query = arg("--search") }
    shotTimer.path = shot
    shotTimer.start()
  }
  Timer {
    id: shotTimer
    property string path: ""
    interval: 900
    onTriggered: card.grabToImage(function(result) {
      result.saveToFile(path)
      Qt.quit()
    })
  }
}
