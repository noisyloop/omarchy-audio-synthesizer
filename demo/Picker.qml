import QtQuick

// Filterable list overlay used for `/` search and for mod-matrix assignment.
// Typing filters; arrows (or Ctrl+j/k) move; Enter picks; Esc cancels.
Rectangle {
  id: picker

  property string title: ""
  property var items: []          // [{ label, data }]
  property string query: ""
  property int index: 0
  readonly property var filtered: {
    var q = query.toLowerCase()
    return items.filter(function(it) { return q === "" || it.label.toLowerCase().indexOf(q) >= 0 })
  }
  signal picked(var data)
  signal cancelled()

  function openWith(t, list) {
    title = t; items = list; query = ""; index = 0
    visible = true
    forceActiveFocus()
  }
  function close() { visible = false }

  visible: false
  color: Qt.alpha(Theme.background, 0.96)
  border.color: Theme.popupBorder
  border.width: 1
  radius: 6

  Keys.onPressed: function(event) {
    var ctrl = event.modifiers & Qt.ControlModifier
    if (event.key === Qt.Key_Escape) { cancelled(); event.accepted = true }
    else if (event.key === Qt.Key_Down || (ctrl && event.key === Qt.Key_J)) { index = Math.min(filtered.length - 1, index + 1); event.accepted = true }
    else if (event.key === Qt.Key_Up || (ctrl && event.key === Qt.Key_K)) { index = Math.max(0, index - 1); event.accepted = true }
    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
      if (filtered.length > 0) picked(filtered[index].data)
      event.accepted = true
    } else if (event.key === Qt.Key_Backspace) { query = query.slice(0, -1); index = 0; event.accepted = true }
    else if (event.text && event.text.length === 1 && event.text >= " " && !ctrl) { query += event.text; index = 0; event.accepted = true }
  }

  Column {
    anchors.fill: parent
    anchors.margins: Theme.padding
    spacing: Theme.gap

    Text {
      text: picker.title
      color: Theme.dim
      font.family: Theme.fontFamily
      font.pixelSize: Theme.caption
    }
    Rectangle {
      width: parent.width
      height: 26
      radius: 4
      color: Theme.faint
      Text {
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        text: "/ " + picker.query + "▏"
        color: Theme.popupText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.body
      }
    }
    ListView {
      id: list
      width: parent.width
      height: parent.height - 70
      clip: true
      model: picker.filtered
      currentIndex: picker.index
      highlightMoveDuration: 0
      delegate: ItemRow {
        required property var modelData
        required property int index
        width: list.width
        label: modelData.label
        current: index === picker.index
      }
    }
  }
}
