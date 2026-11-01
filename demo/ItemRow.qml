import QtQuick

// One focusable row: a parameter, a preset, or a matrix slot.
Rectangle {
  id: row

  property string label: ""
  property string valueText: ""
  property real bar: -1           // 0..1, or -1 for no bar
  property bool bipolar: false
  property bool current: false
  property bool dimmed: false
  property bool marked: false     // favorite / active
  property string mark: "★"
  property int labelWidth: 54

  width: Theme.sectionWidth
  height: Theme.rowHeight
  radius: 3
  color: current ? Theme.cursorFill : "transparent"
  border.width: current ? 1 : 0
  border.color: Theme.accent

  Text {
    id: name
    x: 6
    anchors.verticalCenter: parent.verticalCenter
    width: row.bar >= 0 ? row.labelWidth : Math.min(implicitWidth, parent.width - 30)
    elide: Text.ElideRight
    text: row.label
    color: row.dimmed ? Theme.dim : Theme.popupText
    font.family: Theme.fontFamily
    font.pixelSize: Theme.body
  }

  Rectangle {
    id: track
    visible: row.bar >= 0
    x: row.labelWidth + 10
    width: 28
    height: 4
    radius: 2
    anchors.verticalCenter: parent.verticalCenter
    color: Theme.faint
    Rectangle {
      height: parent.height
      radius: 2
      color: row.dimmed ? Theme.dim : Theme.accent
      x: row.bipolar ? Math.min(parent.width / 2, parent.width * row.bar) : 0
      width: row.bipolar ? Math.abs(parent.width * row.bar - parent.width / 2) : parent.width * row.bar
    }
  }

  Text {
    anchors.right: parent.right
    anchors.rightMargin: 6
    anchors.verticalCenter: parent.verticalCenter
    width: row.bar >= 0 ? parent.width - row.labelWidth - 50 : parent.width - name.width - 18
    horizontalAlignment: Text.AlignRight
    elide: Text.ElideLeft
    text: row.valueText !== "" ? row.valueText : (row.marked ? row.mark : "")
    color: row.marked && row.valueText === ""  ? Theme.accent : (row.dimmed ? Theme.dim : Theme.popupText)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.caption + 1
  }
}
