import QtQuick
import "Model.js" as Model

// The Soundgineer panel: header (status, scope, spectrum), six pages, mod
// matrix, presets, search and piano mode. Keyboard-first.
//
// Shared by the Omarchy plugin (plugin/Widget.qml passes the shell theme and
// plugin/Service.qml) and the standalone demo (demo/Main.qml passes a
// stand-in theme and a mock engine). It must not import qs.* modules.
//
// `engine` provides: powered, patchName, presets [{name, category}],
// favorites {name: true}, matrix [{source, dest, depth, enabled}], held [notes],
// voices, peakL, peakR, scope [], spectrum [], revision, norm(id), setNorm(id, n),
// noteOn(n), noteOff(n), allNotesOff(), loadPreset(name), toggleFavorite(name),
// addSlot(src, dest), updateSlot(i, changes), removeSlot(i), setPower(on),
// files {wavetable: [{name, path}], sample: [...]}, refreshFiles(),
// importWavetable(osc, path), importSample(path).
FocusScope {
  id: win

  required property var theme
  required property var engine
  signal closeRequested()

  implicitWidth: 860
  implicitHeight: 600

  // ------------------------------------------------------------ UI state
  property int page: 0
  property int sec: 0
  property int idx: 0
  property bool piano: false
  property int octave: 4                 // C4 = MIDI 60 on the A key
  property var pianoDown: ({})           // key code -> note
  property var pickerFlow: null          // what the picker result is for
  property string toast: ""

  readonly property var sections: buildSections(page, engine.revision, engine.matrix, engine.favorites, engine.presets)

  // [{ name, names: [...] }] in first-seen order of engine.presets
  function presetCategories() {
    var order = [], by = {}
    var list = engine.presets || []
    for (var i = 0; i < list.length; i++) {
      var c = list[i].category || "Other"
      if (!by[c]) { by[c] = []; order.push(c) }
      by[c].push(list[i].name)
    }
    return order.map(function(c) { return { name: c, names: by[c] } })
  }

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
      var cats = presetCategories()
      for (var c = 0; c < cats.length; c++)
        out.push({ title: cats[c].name.toUpperCase(), items: cats[c].names.map(function(n) { return { kind: "preset", name: n } }) })
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

  // ---- wavetable / sample import (w on an OSC or NOISE section)
  function fileItems(kind) {
    var list = []
    if (kind === "wavetable")
      Model.WAVETABLES.slice(0, 6).forEach(function(n, i) { list.push({ label: "Built-in › " + n, data: { builtin: i } }) })
    var files = (engine.files && engine.files[kind]) || []
    files.forEach(function(f) { list.push({ label: "File › " + f.name, data: { path: f.path } }) })
    if (list.length === 0) list.push({ label: "No WAV files in ~/Music or ~/.local/share/soundgineer/samples", data: { none: true } })
    return list
  }
  function openImport() {
    var title = sections[sec] ? sections[sec].title : ""
    var m = /^OSC (\d)$/.exec(title)
    if (Model.PAGES[page].key === "osc" && m) {
      pickerFlow = { step: "wavetable", osc: Number(m[1]) - 1 }
      if (engine.refreshFiles) engine.refreshFiles()
      picker.openWith("Osc " + m[1] + ": pick a wavetable (Serum-style WAV or single cycle)", fileItems("wavetable"))
    } else if (Model.PAGES[page].key === "osc" && title === "NOISE") {
      pickerFlow = { step: "sample" }
      if (engine.refreshFiles) engine.refreshFiles()
      picker.openWith("Noise: pick a sample", fileItems("sample"))
    } else {
      flash("w imports a wavetable on an OSC section, or a sample on NOISE")
    }
  }
  Connections {
    target: win.engine
    ignoreUnknownSignals: true
    function onFilesChanged() {
      if (picker.visible && win.pickerFlow && (win.pickerFlow.step === "wavetable" || win.pickerFlow.step === "sample"))
        picker.items = win.fileItems(win.pickerFlow.step)
    }
  }

  function openSearch() {
    pickerFlow = { step: "search" }
    var list = Model.order.map(function(id) {
      var p = Model.paramPath(id)
      return p ? { label: p.label, data: { param: id } } : null
    }).filter(function(x) { return x })
    ;(engine.presets || []).forEach(function(p) { list.push({ label: "Preset › " + p.category + " › " + p.name, data: { preset: p.name } }) })
    picker.openWith("Search parameters and presets", list)
  }

  function onPicked(data) {
    var flow = pickerFlow
    if (flow.step === "source") { pickDest(data); return }
    picker.close()
    keys.forceActiveFocus()
    if (flow.step === "wavetable" || flow.step === "sample") {
      if (data.none) return
      if (flow.step === "sample") {
        engine.importSample(data.path)
        engine.setNorm("noise.type", 1)   // Sample
        flash("Sample: " + data.path.split("/").pop())
      } else if (data.builtin !== undefined) {
        engine.setNorm("osc" + (flow.osc + 1) + ".wavetable", data.builtin / 6)
      } else {
        engine.importWavetable(flow.osc, data.path)
        flash("Osc " + (flow.osc + 1) + ": " + data.path.split("/").pop())
      }
      return
    }
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

      if (event.key === Qt.Key_Escape) { win.closeRequested(); event.accepted = true; return }
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
      if (t === "w") { win.openImport(); event.accepted = true; return }
      if (t === "o") { engine.setPower(!engine.powered); event.accepted = true; return }
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
  Item {
    id: card
    anchors.fill: parent

    Column {
      anchors.fill: parent
      spacing: theme.gap + 2

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
            color: theme.dim
            font.family: theme.fontFamily
            font.pixelSize: theme.caption
            font.letterSpacing: 2
          }
          Text {
            width: parent.width
            elide: Text.ElideRight
            text: engine.patchName
            color: theme.popupText
            font.family: theme.fontFamily
            font.pixelSize: theme.heading + 2
            font.bold: true
          }
          Row {
            spacing: 8
            Text {
              text: engine.powered ? "● on" : "○ off"
              color: engine.powered ? theme.accent : theme.dim
              font.family: theme.fontFamily
              font.pixelSize: theme.body
            }
            Row {
              spacing: 2
              anchors.verticalCenter: parent.verticalCenter
              Repeater {
                model: 16
                Rectangle {
                  required property int index
                  width: 5; height: 9; radius: 1
                  color: index < engine.voices ? theme.accent : theme.faint
                }
              }
            }
            Text {
              text: engine.voices + "/16"
              color: theme.dim
              font.family: theme.fontFamily
              font.pixelSize: theme.caption
            }
          }
          Repeater {
            model: [["L", engine.peakL], ["R", engine.peakR]]
            Row {
              required property var modelData
              spacing: 6
              Text {
                text: modelData[0]
                color: theme.dim
                font.family: theme.fontFamily
                font.pixelSize: theme.caption
              }
              Rectangle {
                width: 196; height: 5; radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: theme.faint
                Rectangle {
                  height: parent.height; radius: 2
                  width: parent.width * Math.min(1, modelData[1])
                  color: modelData[1] > 0.95 ? theme.urgent : theme.accent
                }
              }
            }
          }
        }

        Rectangle {
          width: 330; height: header.height; radius: 4
          color: theme.faint
          Canvas {
            id: scopeCanvas
            anchors.fill: parent
            anchors.margins: 4
            property var data: engine.scope
            onDataChanged: requestPaint()
            onPaint: {
              var ctx = getContext("2d")
              ctx.clearRect(0, 0, width, height)
              ctx.strokeStyle = Qt.alpha(theme.foreground, 0.15)
              ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
              if (!data || data.length === 0) return
              ctx.strokeStyle = theme.accent
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
            color: theme.dim
            font.family: theme.fontFamily
            font.pixelSize: theme.caption
          }
        }

        Rectangle {
          width: header.width - 230 - 330 - 28; height: header.height; radius: 4
          color: theme.faint
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
                color: theme.accent
                opacity: 0.85
              }
            }
          }
          Text {
            x: 6; y: 4
            text: "spectrum"
            color: theme.dim
            font.family: theme.fontFamily
            font.pixelSize: theme.caption
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
            color: index === win.page ? theme.cursorFill : "transparent"
            border.width: index === win.page ? 1 : 0
            border.color: theme.accent
            Text {
              id: tabText
              anchors.centerIn: parent
              text: (index + 1) + " " + modelData.title
              color: index === win.page ? theme.popupText : theme.dim
              font.family: theme.fontFamily
              font.pixelSize: theme.body
            }
          }
        }
      }

      Rectangle { width: parent.width; height: 1; color: theme.faint }

      // Page content: sections wrap like text; h/l walk them in order.
      Flickable {
        id: contentFlick
        width: parent.width
        height: card.height - header.height - 24 - 1 - 52 - (win.piano ? 46 : 0) - 6 * (theme.gap + 2)
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
              readonly property int colWidth: modelData.width || theme.sectionWidth
              spacing: 1
              Text {
                text: sectionCol.modelData.title
                color: sectionCol.secIndex === win.sec ? theme.accent : theme.dim
                font.family: theme.fontFamily
                font.pixelSize: theme.caption
                font.letterSpacing: 1
                bottomPadding: 3
              }
              Repeater {
                model: sectionCol.modelData.items
                ItemRow {
                  id: itemRow
                  theme: win.theme
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
            color: (engine.held || []).indexOf(note) >= 0 ? theme.accent : (black ? theme.faint : Qt.alpha(theme.foreground, 0.28))
            Text {
              anchors.bottom: parent.bottom
              anchors.horizontalCenter: parent.horizontalCenter
              bottomPadding: 2
              text: index < 13 ? "AWSEDFTGYHUJK".charAt(index) : ""
              color: theme.background
              font.family: theme.fontFamily
              font.pixelSize: theme.caption
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
          color: theme.dim
          font.family: theme.fontFamily
          font.pixelSize: theme.caption
          text: win.piano
            ? "PIANO  A W S E D F T G Y H U J K play · Z/X octave (C" + win.octave + ") · P or Esc leaves piano mode"
            : "hjkl/arrows move · +/- adjust (_ fine) · Enter toggle/pick · r reset · Tab or 1-6 pages · / search · p piano · o power"
              + (Model.PAGES[win.page].key === "osc" ? " · w import wavetable / sample" : "")
              + (Model.PAGES[win.page].key === "mod" ? " · Enter assign · e on/off · x delete" : "")
              + (Model.PAGES[win.page].key === "presets" ? " · Enter load · f favorite" : "")
        }
        Text {
          height: 14
          text: win.toast
          color: theme.accent
          font.family: theme.fontFamily
          font.pixelSize: theme.caption
        }
      }
    }

    Picker {
      id: picker
      theme: win.theme
      anchors.centerIn: parent
      width: 460
      height: 380
      onPicked: function(data) { win.onPicked(data) }
      onCancelled: { picker.close(); keys.forceActiveFocus() }
    }
  }


  Timer { id: toastTimer; interval: 2200; onTriggered: win.toast = "" }

  /** Leave piano mode and release notes (panel closing, focus lost). */
  function reset() { if (piano) setPiano(false); else engine.allNotesOff(); picker.close(); keys.forceActiveFocus() }
  property alias picker: picker
}
