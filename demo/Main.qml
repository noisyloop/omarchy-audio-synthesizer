import QtQuick
import QtQuick.Window
import "Model.js" as Model

// Soundgineer panel demo. Runs with plain `qml` (no omarchy-shell, no engine):
//
//   qml demo/Main.qml
//
// It previews the keyboard-first panel planned for the Omarchy plugin, driven
// by a mock engine. See demo/README.md for the keys.
Window {
  id: win
  width: 900
  height: 640
  visible: true
  title: "Soundgineer panel demo"
  color: Theme.background

  MockEngine { id: engine }

  // ------------------------------------------------------------ UI state
  property int page: 0
  property int sec: 0
  property int idx: 0
  property bool piano: false
  property int octave: 4                 // C4 = MIDI 60 on the A key
  property var pianoDown: ({})           // key code -> note
  property var pickerFlow: null          // what the picker result is for
  property string toast: ""

  readonly property var sections: buildSections(page, engine.revision, engine.matrix, engine.favorites)

  function buildSections(p) {
    var key = Model.PAGES[p].key
    if (key === "mod") {
      var slots = engine.matrix.map(function(s, i) { return { kind: "slot", i: i } })
      slots.push({ kind: "add" })
      return [{ title: "MOD MATRIX  " + engine.matrix.length + " / 32", width: 420, items: slots }]
    }
    if (key === "presets") {
      var out = []
      var favs = Object.keys(engine.favorites).sort()
      out.push({ title: "★ FAVORITES", items: favs.length ? favs.map(function(n) { return { kind: "preset", name: n } }) : [{ kind: "none" }] })
      for (var cat in Model.PRESETS)
        out.push({ title: cat.toUpperCase(), items: Model.PRESETS[cat].map(function(n) { return { kind: "preset", name: n } }) })
      return out
    }
    return Model.PAGES[p].sections.map(function(s) {
      return { title: s.title, items: s.items.map(function(id) { return { kind: "param", id: id } }) }
    })
  }

  readonly property var currentItem: sections.length && sections[sec] ? sections[sec].items[idx] : null

  function clampCursor() {
    sec = Math.max(0, Math.min(sections.length - 1, sec))
    idx = Math.max(0, Math.min(sections[sec].items.length - 1, idx))
  }
  function goPage(p) {
    page = (p + Model.PAGES.length) % Model.PAGES.length
    sec = 0; idx = 0
  }
  function ensureVisible(item) {
    var y = item.mapToItem(flow, 0, 0).y
    if (y - 18 < contentFlick.contentY) contentFlick.contentY = Math.max(0, y - 18)
    else if (y + item.height > contentFlick.contentY + contentFlick.height)
      contentFlick.contentY = y + item.height - contentFlick.height + 4
  }
  function flash(text) { toast = text; toastTimer.restart() }

  // ------------------------------------------------------------ labels
  function sourceLabel(src) {
    var m = /^(env|lfo|macro)(\d)$/.exec(src)
    if (m) return { env: "Env ", lfo: "LFO ", macro: "Macro " }[m[1]] + m[2]
    return { velocity: "Velocity", keytrack: "Key Track", random: "Random", modwheel: "Mod Wheel",
             pitchwheel: "Pitch Whl", aftertouch: "Pressure" }[src] || src
  }
  function destLabel(id) {
    var path = Model.paramPath(id)
    if (!path) return id
    var t = Model.PAGES[path.page].sections[path.section].title
    return t.charAt(0) + t.slice(1).toLowerCase() + " " + Model.params[id].name
  }

  // ------------------------------------------------------------ actions
  function adjust(dir, fine) {
    var it = currentItem
    if (!it) return
    if (it.kind === "param") {
      var d = Model.params[it.id]
      engine.setNorm(it.id, Model.stepped(d, engine.norm(it.id), dir, fine))
    } else if (it.kind === "slot") {
      var s = engine.matrix[it.i]
      engine.updateSlot(it.i, { depth: Math.max(-1, Math.min(1, Math.round((s.depth + dir * (fine ? 0.002 : 0.02)) * 1000) / 1000)) })
    }
  }

  function activate() {
    var it = currentItem
    if (!it) return
    if (it.kind === "param") {
      var d = Model.params[it.id]
      if (d.step && (d.fmt === "bool" || d.fmt === "choice")) {
        var n = engine.norm(it.id)
        engine.setNorm(it.id, n >= 0.999 ? 0 : Model.stepped(d, n, 1, false))
      }
    } else if (it.kind === "preset") {
      engine.loadPreset(it.name)
      flash("Loaded " + it.name)
    } else if (it.kind === "slot" || it.kind === "add") {
      pickSource(it.kind === "slot" ? it.i : -1)
    }
  }

  function pickSource(slot) {
    pickerFlow = { step: "source", slot: slot }
    picker.openWith(slot < 0 ? "New slot: pick a source" : "Slot " + (slot + 1) + ": pick a source",
      Model.MOD_SOURCES.map(function(s) { return { label: sourceLabel(s), data: s } }))
  }
  function pickDest(source) {
    pickerFlow = { step: "dest", slot: pickerFlow.slot, source: source }
    picker.openWith(sourceLabel(source) + " → pick a target",
      Model.moddableIds().map(function(id) { return { label: destLabel(id), data: id } }))
  }

  function openSearch() {
    pickerFlow = { step: "search" }
    var list = Model.order.map(function(id) {
      var p = Model.paramPath(id)
      return p ? { label: p.label, data: { param: id } } : null
    }).filter(function(x) { return x })
    for (var cat in Model.PRESETS)
      Model.PRESETS[cat].forEach(function(n) { list.push({ label: "Preset › " + cat + " › " + n, data: { preset: n } }) })
    picker.openWith("Search parameters and presets", list)
  }

  function onPicked(data) {
    var flow = pickerFlow
    if (flow.step === "source") { pickDest(data); return }
    picker.close()
    keys.forceActiveFocus()
    if (flow.step === "dest") {
      if (flow.slot < 0) {
        var i = engine.addSlot(flow.source, data)
        if (i >= 0) idx = i
      } else {
        engine.updateSlot(flow.slot, { source: flow.source, dest: data })
      }
      flash(sourceLabel(flow.source) + " → " + destLabel(data))
    } else if (flow.step === "search") {
      if (data.preset) {
        engine.loadPreset(data.preset)
        flash("Loaded " + data.preset)
      } else {
        var p = Model.paramPath(data.param)
        page = p.page; sec = p.section
        idx = Model.PAGES[p.page].sections[p.section].items.indexOf(data.param)
      }
    }
  }

  // ------------------------------------------------------------ keyboard
  readonly property var pianoMap: ({
    [Qt.Key_A]: 0, [Qt.Key_W]: 1, [Qt.Key_S]: 2, [Qt.Key_E]: 3, [Qt.Key_D]: 4, [Qt.Key_F]: 5,
    [Qt.Key_T]: 6, [Qt.Key_G]: 7, [Qt.Key_Y]: 8, [Qt.Key_H]: 9, [Qt.Key_U]: 10, [Qt.Key_J]: 11, [Qt.Key_K]: 12
  })

  function setPiano(on) {
    piano = on
    if (!on) { pianoDown = ({}); engine.allNotesOff() }
  }

  Item {
    id: keys
    anchors.fill: parent
    focus: true

    Keys.onReleased: function(event) {
      if (!win.piano || event.isAutoRepeat) return
      var note = win.pianoDown[event.key]
      if (note !== undefined) {
        engine.noteOff(note)
        var d = win.pianoDown; delete d[event.key]; win.pianoDown = d
        event.accepted = true
      }
    }

    Keys.onPressed: function(event) {
      var shift = event.modifiers & Qt.ShiftModifier
      var t = event.text

      if (win.piano) {
        // Piano mode owns the letter keys, so h/j/k and x play notes here.
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_P) { win.setPiano(false); event.accepted = true; return }
        if (event.isAutoRepeat) { event.accepted = true; return }
        if (event.key === Qt.Key_Z) { win.octave = Math.max(0, win.octave - 1); event.accepted = true; return }
        if (event.key === Qt.Key_X) { win.octave = Math.min(8, win.octave + 1); event.accepted = true; return }
        var semi = win.pianoMap[event.key]
        if (semi !== undefined) {
          var note = 12 * (win.octave + 1) + semi
          engine.noteOn(note)
          var d = win.pianoDown; d[event.key] = note; win.pianoDown = d
          event.accepted = true
        }
        return
      }

      if (event.key === Qt.Key_Escape) { win.flash("Esc closes the panel in Omarchy"); event.accepted = true; return }
      if (event.key === Qt.Key_Tab) { win.goPage(win.page + 1); event.accepted = true; return }
      if (event.key === Qt.Key_Backtab) { win.goPage(win.page - 1); event.accepted = true; return }
      if (event.key === Qt.Key_Down || t === "j") { win.idx++; win.clampCursor(); event.accepted = true; return }
      if (event.key === Qt.Key_Up || t === "k") { win.idx--; win.clampCursor(); event.accepted = true; return }
      if (event.key === Qt.Key_Right || t === "l") { win.sec++; win.clampCursor(); event.accepted = true; return }
      if (event.key === Qt.Key_Left || t === "h") { win.sec--; win.clampCursor(); event.accepted = true; return }
      if (t === "+" || t === "=") { win.adjust(1, false); event.accepted = true; return }
      if (t === "-" || t === "_") { win.adjust(-1, t === "_"); event.accepted = true; return }
      if (event.key === Qt.Key_Plus && shift) { win.adjust(1, true); event.accepted = true; return }
      if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { win.activate(); event.accepted = true; return }
      if (t >= "1" && t <= "6") { win.goPage(Number(t) - 1); event.accepted = true; return }
      if (t === "/") { win.openSearch(); event.accepted = true; return }
      if (t === "p") { win.setPiano(true); event.accepted = true; return }
      if (t === "o") { engine.powered = !engine.powered; event.accepted = true; return }
      var it = win.currentItem
      if (t === "r" && it && it.kind === "param") {
        engine.setNorm(it.id, Model.toNorm(Model.params[it.id], Model.params[it.id].def))
        event.accepted = true; return
      }
      if (t === "f" && it && it.kind === "preset") { engine.toggleFavorite(it.name); event.accepted = true; return }
      if ((t === "x" || event.key === Qt.Key_Delete) && it && it.kind === "slot") {
        engine.removeSlot(it.i); win.clampCursor(); event.accepted = true; return
      }
      if (t === "e" && it && it.kind === "slot") {
        engine.updateSlot(it.i, { enabled: !engine.matrix[it.i].enabled }); event.accepted = true; return
      }
    }
  }

  // ------------------------------------------------------------ layout
  Rectangle {
    id: card
    anchors.fill: parent
    anchors.margins: 10
    radius: 8
    color: Theme.popupBackground
    border.color: Theme.popupBorder
    border.width: 2

    Column {
      anchors.fill: parent
      anchors.margins: Theme.padding
      spacing: Theme.gap + 2

      // Header: status on the left, scope + spectrum on the right.
      Row {
        id: header
        width: parent.width
        height: 104
        spacing: 14

        Column {
          width: 230
          spacing: 5
          Text {
            text: "SOUNDGINEER"
            color: Theme.dim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.caption
            font.letterSpacing: 2
          }
          Text {
            width: parent.width
            elide: Text.ElideRight
            text: engine.patchName
            color: Theme.popupText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.heading + 2
            font.bold: true
          }
          Row {
            spacing: 8
            Text {
              text: engine.powered ? "● on" : "○ off"
              color: engine.powered ? Theme.accent : Theme.dim
              font.family: Theme.fontFamily
              font.pixelSize: Theme.body
            }
            Row {
              spacing: 2
              anchors.verticalCenter: parent.verticalCenter
              Repeater {
                model: 16
                Rectangle {
                  required property int index
                  width: 5; height: 9; radius: 1
                  color: index < engine.voices ? Theme.accent : Theme.faint
                }
              }
            }
            Text {
              text: engine.voices + "/16"
              color: Theme.dim
              font.family: Theme.fontFamily
              font.pixelSize: Theme.caption
            }
          }
          Repeater {
            model: [["L", engine.peakL], ["R", engine.peakR]]
            Row {
              required property var modelData
              spacing: 6
              Text {
                text: modelData[0]
                color: Theme.dim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.caption
              }
              Rectangle {
                width: 196; height: 5; radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.faint
                Rectangle {
                  height: parent.height; radius: 2
                  width: parent.width * Math.min(1, modelData[1])
                  color: modelData[1] > 0.95 ? Theme.urgent : Theme.accent
                }
              }
            }
          }
        }

        Rectangle {
          width: 330; height: header.height; radius: 4
          color: Theme.faint
          Canvas {
            id: scopeCanvas
            anchors.fill: parent
            anchors.margins: 4
            property var data: engine.scope
            onDataChanged: requestPaint()
            onPaint: {
              var ctx = getContext("2d")
              ctx.clearRect(0, 0, width, height)
              ctx.strokeStyle = Qt.alpha(Theme.foreground, 0.15)
              ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
              if (!data || data.length === 0) return
              ctx.strokeStyle = Theme.accent
              ctx.lineWidth = 1.5
              ctx.beginPath()
              for (var i = 0; i < data.length; i++) {
                var x = i / (data.length - 1) * width
                var y = height / 2 - data[i] * height * 0.45
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
              }
              ctx.stroke()
            }
          }
          Text {
            x: 6; y: 4
            text: "scope"
            color: Theme.dim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.caption
          }
        }

        Rectangle {
          width: header.width - 230 - 330 - 28; height: header.height; radius: 4
          color: Theme.faint
          Row {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 1
            Repeater {
              model: engine.spectrum.length
              Rectangle {
                required property int index
                anchors.bottom: parent.bottom
                width: Math.max(2, (header.width - 590) / 48 - 1)
                height: Math.max(1, (header.height - 22) * engine.spectrum[index])
                color: Theme.accent
                opacity: 0.85
              }
            }
          }
          Text {
            x: 6; y: 4
            text: "spectrum"
            color: Theme.dim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.caption
          }
        }
      }

      // Page tabs
      Row {
        spacing: 4
        Repeater {
          model: Model.PAGES
          Rectangle {
            required property var modelData
            required property int index
            width: tabText.implicitWidth + 20
            height: 24
            radius: 4
            color: index === win.page ? Theme.cursorFill : "transparent"
            border.width: index === win.page ? 1 : 0
            border.color: Theme.accent
            Text {
              id: tabText
              anchors.centerIn: parent
              text: (index + 1) + " " + modelData.title
              color: index === win.page ? Theme.popupText : Theme.dim
              font.family: Theme.fontFamily
              font.pixelSize: Theme.body
            }
          }
        }
      }

      Rectangle { width: parent.width; height: 1; color: Theme.faint }

      // Page content: sections wrap like text; h/l walk them in order.
      Flickable {
        id: contentFlick
        width: parent.width
        height: card.height - header.height - 24 - 1 - 52 - (win.piano ? 46 : 0) - Theme.padding * 2 - 6 * (Theme.gap + 2)
        clip: true
        contentHeight: flow.implicitHeight
        Flow {
          id: flow
          width: parent.width
          spacing: 12
          Repeater {
            model: win.sections
            Column {
              id: sectionCol
              required property var modelData
              required property int index
              readonly property int secIndex: index
              readonly property int colWidth: modelData.width || Theme.sectionWidth
              spacing: 1
              Text {
                text: sectionCol.modelData.title
                color: sectionCol.secIndex === win.sec ? Theme.accent : Theme.dim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.caption
                font.letterSpacing: 1
                bottomPadding: 3
              }
              Repeater {
                model: sectionCol.modelData.items
                ItemRow {
                  id: itemRow
                  required property var modelData
                  required property int index
                  readonly property var d: modelData.kind === "param" ? Model.params[modelData.id] : null
                  readonly property var slot: modelData.kind === "slot" ? engine.matrix[modelData.i] : null
                  readonly property int rev: engine.revision
                  width: sectionCol.colWidth
                  labelWidth: slot || modelData.kind === "add" ? sectionCol.colWidth - 130 : 54
                  onCurrentChanged: if (current) win.ensureVisible(itemRow)
                  current: sectionCol.secIndex === win.sec && index === win.idx
                  label: {
                    rev
                    if (d) return d.name
                    if (slot) return (slot.enabled ? "" : "(off) ") + win.sourceLabel(slot.source) + " → " + win.destLabel(slot.dest)
                    if (modelData.kind === "add") return "+ add slot"
                    if (modelData.kind === "none") return "f marks a favorite"
                    return modelData.name
                  }
                  bar: {
                    rev
                    if (d) return d.fmt === "choice" ? -1 : engine.norm(d.id)
                    if (slot) return (slot.depth + 1) / 2
                    return -1
                  }
                  bipolar: !!slot || (!!d && d.min < 0)
                  valueText: {
                    rev
                    if (d) return Model.format(d, engine.norm(d.id))
                    if (slot) return (slot.depth >= 0 ? "+" : "") + slot.depth.toFixed(2)
                    return ""
                  }
                  dimmed: {
                    rev
                    if (slot) return !slot.enabled
                    if (modelData.kind === "add" || modelData.kind === "none") return true
                    if (!d) return false
                    var grp = d.id.split(".")[0]
                    var on = Model.params[grp + ".enabled"]
                    return !!on && d.id !== grp + ".enabled" && engine.norm(grp + ".enabled") < 0.5
                  }
                  marked: modelData.kind === "preset" && (rev, !!engine.favorites[modelData.name] || engine.patchName === modelData.name)
                  mark: modelData.kind === "preset" && engine.patchName === modelData.name ? "▶" : "★"
                }
              }
            }
          }
        }
      }

      // Piano strip (only while piano mode is on)
      Row {
        visible: win.piano
        height: win.piano ? 46 : 0
        spacing: 2
        Repeater {
          model: 25
          Rectangle {
            required property int index
            readonly property int note: 12 * (win.octave + 1) + index - 0
            readonly property bool black: [1, 3, 6, 8, 10].indexOf(index % 12) >= 0
            width: 26; height: 46; radius: 3
            color: engine.held.indexOf(note) >= 0 ? Theme.accent : (black ? Theme.faint : Qt.alpha(Theme.foreground, 0.28))
            Text {
              anchors.bottom: parent.bottom
              anchors.horizontalCenter: parent.horizontalCenter
              bottomPadding: 2
              text: index < 13 ? "AWSEDFTGYHUJK".charAt(index) : ""
              color: Theme.background
              font.family: Theme.fontFamily
              font.pixelSize: Theme.caption
            }
          }
        }
      }

      // Footer: context hints + toast
      Column {
        width: parent.width
        spacing: 3
        Text {
          width: parent.width
          wrapMode: Text.Wrap
          color: Theme.dim
          font.family: Theme.fontFamily
          font.pixelSize: Theme.caption
          text: win.piano
            ? "PIANO  A W S E D F T G Y H U J K play · Z/X octave (C" + win.octave + ") · P or Esc leaves piano mode"
            : "hjkl/arrows move · +/- adjust (_ fine) · Enter toggle/pick · r reset · Tab or 1-6 pages · / search · p piano · o power"
              + (Model.PAGES[win.page].key === "mod" ? " · Enter assign · e on/off · x delete" : "")
              + (Model.PAGES[win.page].key === "presets" ? " · Enter load · f favorite" : "")
        }
        Text {
          height: 14
          text: win.toast
          color: Theme.accent
          font.family: Theme.fontFamily
          font.pixelSize: Theme.caption
        }
      }
    }

    Picker {
      id: picker
      anchors.centerIn: parent
      width: 460
      height: 380
      onPicked: function(data) { win.onPicked(data) }
      onCancelled: { picker.close(); keys.forceActiveFocus() }
    }
  }

  Timer { id: toastTimer; interval: 2200; onTriggered: win.toast = "" }

  // ------------------------------------------------------------ screenshots
  //   qml demo/Main.qml -- --screenshot out.png [--page 1-6] [--notes 60,64,67] [--piano] [--search text]
  function arg(name) {
    var a = Qt.application.arguments
    var i = a.indexOf(name)
    return i >= 0 && i + 1 < a.length ? a[i + 1] : ""
  }
  Component.onCompleted: {
    var shot = arg("--screenshot")
    if (shot === "") return
    if (arg("--page") !== "") goPage(Number(arg("--page")) - 1)
    if (arg("--preset") !== "") engine.loadPreset(arg("--preset"))
    if (arg("--cursor") !== "") { var c = arg("--cursor").split(","); sec = Number(c[0]); idx = Number(c[1]) }
    if (Qt.application.arguments.indexOf("--piano") >= 0) piano = true
    if (arg("--notes") !== "") arg("--notes").split(",").forEach(function(n) { engine.noteOn(Number(n)) })
    if (arg("--search") !== "") { openSearch(); picker.query = arg("--search") }
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
