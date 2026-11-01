import QtQuick
import "../plugin/ui/Model.js" as Model

// Stands in for plugin/Service.qml + soundgineer-engine: holds the patch,
// "plays" held notes, and produces scope/spectrum/peak telemetry at 30 Hz so
// the panel behaves like the real thing. No audio is produced.
QtObject {
  id: engine

  property bool powered: true
  readonly property var presets: {
    var out = []
    for (var cat in Model.PRESETS)
      Model.PRESETS[cat].forEach(function(n) { out.push({ name: n, category: cat }) })
    return out
  }
  function setPower(on) { powered = on; if (!on) held = [] }
  property var files: ({ wavetable: [], sample: [] })
  function refreshFiles() {
    files = { wavetable: [{ name: "Serum Saws", path: "~/Music/wavetables/Serum Saws.wav" }, { name: "Vowel Morph", path: "~/Music/wavetables/Vowel Morph.wav" }],
              sample: [{ name: "Vinyl Crackle", path: "~/Music/samples/Vinyl Crackle.wav" }] }
  }
  function importWavetable(osc, path) { setNorm("osc" + (osc + 1) + ".wavetable", 1) }
  function importSample(path) {}
  property string patchName: "Init"
  property var values: Model.defaults()
  property var matrix: [
    { source: "lfo1", dest: "osc1.morph", depth: 0.35, enabled: true },
    { source: "modwheel", dest: "osc1.morph", depth: 0.5, enabled: true },
    { source: "env2", dest: "filter1.cutoff", depth: 0.45, enabled: true }
  ]
  property var favorites: ({ "Super Saw Lead": true, "Warm Analog Pad": true })
  property var held: []                 // MIDI notes currently down
  readonly property int voices: powered ? held.length : 0
  property real peakL: 0
  property real peakR: 0
  property var scope: []                // 256 points, -1..1
  property var spectrum: []             // 48 bins, 0..1
  property real time: 0
  property int revision: 0              // bumps on every value change, for bindings

  function norm(id) { return values[id] !== undefined ? values[id] : 0 }
  function setNorm(id, n) {
    var next = values
    next[id] = Math.max(0, Math.min(1, n))
    values = next
    revision++
  }

  function noteOn(note) {
    if (held.indexOf(note) >= 0) return
    held = held.concat([note]).slice(-16)   // 16-voice cap, oldest stolen
  }
  function noteOff(note) { held = held.filter(function(n) { return n !== note }) }
  function allNotesOff() { held = [] }

  function toggleFavorite(name) {
    var f = favorites
    if (f[name]) delete f[name]; else f[name] = true
    favorites = f
    revision++
  }

  // Deterministic stand-in for loading a factory preset: reset to defaults
  // and nudge a few parameters from a hash of the name.
  function loadPreset(name) {
    var v = Model.defaults()
    var h = 0
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) >>> 0
    function r() { h = (h * 1103515245 + 12345) >>> 0; return (h >>> 8) / 16777216 }
    if (name !== "Init") {
      v["osc1.morph"] = r(); v["osc1.unison"] = Math.round(r() * 6) / 15
      v["filter1.cutoff"] = 0.35 + r() * 0.6; v["filter1.resonance"] = r() * 0.6
      v["env1.attack"] = r() * 0.5; v["env1.release"] = 0.2 + r() * 0.5
      v["reverb.enabled"] = r() > 0.5 ? 1 : 0; v["chorus.enabled"] = r() > 0.6 ? 1 : 0
    }
    values = v
    patchName = name
    revision++
  }

  function addSlot(source, dest) {
    if (matrix.length >= 32) return -1
    matrix = matrix.concat([{ source: source, dest: dest, depth: 0.5, enabled: true }])
    revision++
    return matrix.length - 1
  }
  function updateSlot(i, changes) {
    var m = matrix.slice()
    var s = Object.assign({}, m[i])
    for (var k in changes) s[k] = changes[k]
    m[i] = s
    matrix = m
    revision++
  }
  function removeSlot(i) {
    var m = matrix.slice()
    m.splice(i, 1)
    matrix = m
    revision++
  }

  property Timer ticker: Timer {
    interval: 33          // ≤ 30 Hz, like the real telemetry
    running: true
    repeat: true
    onTriggered: engine.tick()
  }

  function tick() {
    time += 0.033
    var morph = norm("osc1.morph")
    var cutoff = norm("filter1.cutoff")
    var level = norm("master.volume") / 0.7 * 0.7
    var notes = powered ? held : []
    var pts = []
    for (var i = 0; i < 256; i++) {
      var s = 0
      for (var k = 0; k < notes.length; k++) {
        var cycles = 2 * Math.pow(2, (notes[k] - 60) / 12)
        var ph = (i / 256 * cycles + time * 0.25) % 1
        var sine = Math.sin(2 * Math.PI * ph)
        var saw = 2 * ph - 1
        s += (sine * (1 - morph) + saw * morph) * (0.4 + 0.6 * cutoff)
      }
      pts.push(Math.max(-1, Math.min(1, s * level / Math.max(1, Math.sqrt(notes.length)))))
    }
    scope = pts

    var bins = []
    for (var b = 0; b < 48; b++) {
      var amp = 0
      for (var n = 0; n < notes.length; n++) {
        for (var hmn = 1; hmn <= 12; hmn++) {
          var pos = 4 + (notes[n] - 36) * 0.3 + 7 * Math.log(hmn) / Math.LN2
          var w = Math.exp(-Math.pow(b - pos, 2) / 2)
          var harm = hmn === 1 ? 1 : morph / hmn
          var lp = 1 / (1 + Math.pow(pos / (4 + cutoff * 44), 4))
          amp += w * harm * lp
        }
      }
      bins.push(Math.min(1, amp * 0.8))
    }
    spectrum = bins

    var target = notes.length > 0 ? Math.min(1, 0.45 + 0.08 * notes.length) * level : 0
    peakL = Math.max(target * (0.92 + 0.08 * Math.sin(time * 9)), peakL * 0.82)
    peakR = Math.max(target * (0.92 + 0.08 * Math.cos(time * 7)), peakR * 0.82)
  }
}
