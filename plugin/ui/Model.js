.pragma library

// Parameter, page and preset data for the demo. IDs, ranges, defaults and
// curves follow the reference parameter registry; the real plugin will get
// all of this from the engine (`getState`) instead.

var FILTER_TYPES = ["LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24", "Notch", "Comb", "Formant"]
var SUB_SHAPES = ["Sine", "Triangle", "Saw", "Square"]
var NOISE_TYPES = ["White", "Pink", "Sample"]
var DIST_TYPES = ["Off", "Soft Clip", "Hard Clip", "Wavefold", "Bitcrush"]
var ROUTINGS = ["Series", "Parallel"]
var WAVETABLES = ["Basic Shapes", "Harmonic Sweep", "PWM", "Vocal", "FM Bell", "Digital", "Custom"]
var LFO_MODES = ["Trigger", "Free", "Sync"]
var DIVISIONS = ["1/1", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32"]

var MOD_SOURCES = ["env1", "env2", "env3", "env4", "env5", "env6",
  "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "lfo7", "lfo8",
  "velocity", "keytrack", "random", "macro1", "macro2", "macro3", "macro4",
  "modwheel", "pitchwheel", "aftertouch"]

var params = {}   // id -> def
var order = []    // ids in registry order

function p(id, name, min, max, def, fmt, opts) {
  var d = { id: id, name: name, min: min, max: max, def: def, fmt: fmt || "raw" }
  if (opts) for (var k in opts) d[k] = opts[k]
  if (d.choices) { d.min = 0; d.max = d.choices.length - 1; d.step = 1; d.fmt = "choice" }
  params[id] = d
  order.push(id)
}

var M = { moddable: true }
function mexp() { return { moddable: true, curve: "exp" } }

p("master.volume", "Master", 0, 1.5, 0.7, "pct", M)
p("master.polyphony", "Voices", 1, 16, 16, "int", { step: 1 })
p("master.bend_range", "Bend Rng", 1, 48, 2, "st", { step: 1 })
for (var o = 1; o <= 3; o++) {
  var g = "osc" + o
  p(g + ".enabled", "On", 0, 1, o === 1 ? 1 : 0, "bool", { step: 1 })
  p(g + ".wavetable", "Table", 0, 0, 0, "", { choices: WAVETABLES })
  p(g + ".morph", "Morph", 0, 1, 0, "pct", M)
  p(g + ".level", "Level", 0, 1, 0.7, "pct", M)
  p(g + ".pan", "Pan", -1, 1, 0, "pan", M)
  p(g + ".unison", "Unison", 1, 16, 1, "voices", { step: 1 })
  p(g + ".detune", "Detune", 0, 100, 12, "ct", M)
  p(g + ".blend", "Blend", 0, 1, 0.7, "pct", M)
  p(g + ".transpose", "Pitch", -48, 48, 0, "st", { step: 1, moddable: true })
  p(g + ".fine", "Fine", -100, 100, 0, "ct", M)
  p(g + ".sync", "Sync", 1, 4, 1, "x", M)
}
p("sub.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("sub.shape", "Shape", 0, 0, 0, "", { choices: SUB_SHAPES })
p("sub.level", "Level", 0, 1, 0.6, "pct", M)
p("sub.octave", "Octave", -3, 0, -1, "oct", { step: 1 })
p("noise.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("noise.type", "Type", 0, 0, 0, "", { choices: NOISE_TYPES })
p("noise.level", "Level", 0, 1, 0.5, "pct", M)
p("noise.pitch", "Pitch", -24, 24, 0, "st", M)
for (var f = 1; f <= 2; f++) {
  var fg = "filter" + f
  p(fg + ".enabled", "On", 0, 1, f === 1 ? 1 : 0, "bool", { step: 1 })
  p(fg + ".type", "Type", 0, 0, 1, "", { choices: FILTER_TYPES })
  p(fg + ".cutoff", "Cutoff", 20, 20000, 8000, "hz", mexp())
  p(fg + ".resonance", "Res", 0, 1, 0.2, "pct", M)
  p(fg + ".drive", "Drive", 0, 1, 0, "pct", M)
  p(fg + ".keytrack", "Key Trk", 0, 1, 0, "pct", M)
  p(fg + ".mix", "Mix", 0, 1, 1, "pct", M)
}
p("filter.routing", "Routing", 0, 0, 0, "", { choices: ROUTINGS })
p("dist.type", "Type", 0, 0, 0, "", { choices: DIST_TYPES })
p("dist.drive", "Drive", 0, 1, 0.3, "pct", M)
p("dist.mix", "Mix", 0, 1, 1, "pct", M)
p("dist.bits", "Bits", 1, 16, 8, "bits", M)
p("dist.downsample", "Rate", 1, 64, 1, "div", mexp())
for (var e = 1; e <= 2; e++) {
  var eg = "env" + e
  p(eg + ".delay", "Delay", 0, 2, 0, "ms")
  p(eg + ".attack", "Attack", 0.001, 10, e === 1 ? 0.005 : 0.05, "ms", mexp())
  p(eg + ".hold", "Hold", 0, 2, 0, "ms")
  p(eg + ".decay", "Decay", 0.001, 10, 0.5, "ms", mexp())
  p(eg + ".sustain", "Sustain", 0, 1, e === 1 ? 0.8 : 0.5, "pct", M)
  p(eg + ".release", "Release", 0.002, 15, 0.2, "ms", mexp())
}
p("lfo1.rate", "Rate", 0.01, 40, 2, "hz", mexp())
p("lfo1.sync", "Sync", 0, 1, 1, "bool", { step: 1 })
p("lfo1.division", "Div", 0, 0, 4, "", { choices: DIVISIONS })
p("lfo1.mode", "Mode", 0, 0, 0, "", { choices: LFO_MODES })
p("lfo1.smooth", "Smooth", 0, 1, 0, "pct")
for (var m = 1; m <= 4; m++) p("macro" + m + ".value", "Macro " + m, 0, 1, 0, "pct", M)
p("chorus.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("chorus.rate", "Rate", 0.05, 8, 0.4, "hz", mexp())
p("chorus.depth", "Depth", 0, 1, 0.5, "pct", M)
p("chorus.mix", "Mix", 0, 1, 0.5, "pct", M)
p("phaser.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("phaser.rate", "Rate", 0.02, 10, 0.3, "hz", mexp())
p("phaser.feedback", "Fdbk", 0, 0.95, 0.4, "pct", M)
p("phaser.mix", "Mix", 0, 1, 0.5, "pct", M)
p("flanger.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("flanger.rate", "Rate", 0.02, 10, 0.25, "hz", mexp())
p("flanger.feedback", "Fdbk", 0, 0.95, 0.5, "pct", M)
p("flanger.mix", "Mix", 0, 1, 0.5, "pct", M)
p("delay.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("delay.division", "Div", 0, 0, 7, "", { choices: DIVISIONS })
p("delay.feedback", "Fdbk", 0, 0.98, 0.4, "pct", M)
p("delay.pingpong", "PingPong", 0, 1, 0, "bool", { step: 1 })
p("delay.mix", "Mix", 0, 1, 0.3, "pct", M)
p("reverb.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("reverb.size", "Size", 0, 1, 0.7, "pct", M)
p("reverb.damp", "Damp", 0, 1, 0.5, "pct", M)
p("reverb.mix", "Mix", 0, 1, 0.3, "pct", M)
p("eq.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("eq.low_gain", "Low", -18, 18, 0, "db", M)
p("eq.mid_gain", "Mid", -18, 18, 0, "db", M)
p("eq.high_gain", "High", -18, 18, 0, "db", M)
p("comp.enabled", "On", 0, 1, 0, "bool", { step: 1 })
p("comp.threshold", "Thresh", -60, 0, -18, "db", M)
p("comp.ratio", "Ratio", 1, 20, 4, "ratio", { curve: "exp" })

function ids(prefix, names) {
  return names.map(function(n) { return prefix + "." + n })
}
var OSC = ["enabled", "wavetable", "morph", "level", "pan", "unison", "detune", "blend", "transpose", "fine", "sync"]
var FILT = ["enabled", "type", "cutoff", "resonance", "drive", "keytrack", "mix"]
var ENV = ["delay", "attack", "hold", "decay", "sustain", "release"]

var PAGES = [
  { key: "osc", title: "Osc", sections: [
    { title: "OSC 1", items: ids("osc1", OSC) },
    { title: "OSC 2", items: ids("osc2", OSC) },
    { title: "OSC 3", items: ids("osc3", OSC) },
    { title: "SUB", items: ids("sub", ["enabled", "shape", "level", "octave"]) },
    { title: "NOISE", items: ids("noise", ["enabled", "type", "level", "pitch"]) } ] },
  { key: "filter", title: "Filter", sections: [
    { title: "FILTER 1", items: ids("filter1", FILT) },
    { title: "FILTER 2", items: ids("filter2", FILT) },
    { title: "ROUTING", items: ["filter.routing"] } ] },
  { key: "shape", title: "Shape", sections: [
    { title: "SHAPE", items: ids("dist", ["type", "drive", "mix", "bits", "downsample"]) },
    { title: "AMP ENV", items: ids("env1", ENV) },
    { title: "ENV 2", items: ids("env2", ENV) },
    { title: "LFO 1", items: ids("lfo1", ["rate", "sync", "division", "mode", "smooth"]) },
    { title: "MACROS", items: ["macro1.value", "macro2.value", "macro3.value", "macro4.value"] } ] },
  { key: "mod", title: "Mod", sections: [] },        // built from the matrix
  { key: "fx", title: "FX", sections: [
    { title: "CHORUS", items: ids("chorus", ["enabled", "rate", "depth", "mix"]) },
    { title: "PHASER", items: ids("phaser", ["enabled", "rate", "feedback", "mix"]) },
    { title: "FLANGER", items: ids("flanger", ["enabled", "rate", "feedback", "mix"]) },
    { title: "DELAY", items: ids("delay", ["enabled", "division", "feedback", "pingpong", "mix"]) },
    { title: "REVERB", items: ids("reverb", ["enabled", "size", "damp", "mix"]) },
    { title: "EQ", items: ids("eq", ["enabled", "low_gain", "mid_gain", "high_gain"]) },
    { title: "COMP", items: ids("comp", ["enabled", "threshold", "ratio"]) },
    { title: "MASTER", items: ["master.volume", "master.polyphony", "master.bend_range"] } ] },
  { key: "presets", title: "Presets", sections: [] }  // built from PRESETS
]

// Factory preset names in reference order, grouped by the reference's own
// section comments (bass, leads, pads, keys, plucks, fx / other); the
// uncategorized first five are grouped as "Starter".
var PRESETS = {
  "Starter": ["Init", "Deep Saw Bass", "Morphing Pad", "Sync Pluck", "PWM Keys"],
  "Bass": ["Reese Bass", "Acid Squelch", "Wobble Bass",
    "FM Knock", "Solid Square", "Neuro Growl", "808 Drop", "Pulse Sub", "Fold Bass", "Mid Bass Formant", "Rumble Sine"],
  "Leads": ["Super Saw Lead", "Sync Screamer", "Breath Flute", "Chip Lead", "Vox Lead", "Crystal Bell",
    "Trance Pluck Lead", "Fuzz Mono", "Jet Lead", "Glass Whistle"],
  "Pads": ["Warm Analog Pad", "Choir Pad", "Shimmer Pad", "Dark Matter", "Glass Pad", "Aurora Texture",
    "Phase Cathedral", "Jet Stream Pad", "Granite Drone", "Velvet Strings"],
  "Keys": ["Lo-Fi EP", "Drawbar Organ", "Funk Clav", "Rave Stab", "Phased Rhodes", "Toy Piano", "Wurly Grit", "Hollow Organ"],
  "Plucks": ["Ice Pluck", "Kalimba", "Rubber Pluck", "Arp Nights", "Koto Pluck", "Bit Pluck", "Marimba Wood", "Comb Harp"],
  "FX": ["Tension Riser", "Ocean Drift", "Laser Zap", "Alarm Siren", "Robot Talk", "Downlifter", "Vinyl Haze"]
}

// ------------------------------------------------------------ value mapping
function toValue(d, n) {
  var v = d.curve === "exp" ? d.min * Math.pow(d.max / d.min, n) : d.min + (d.max - d.min) * n
  if (d.step) v = Math.round(v / d.step) * d.step
  return v
}

function toNorm(d, v) {
  if (d.max === d.min) return 0
  var n = d.curve === "exp" ? Math.log(v / d.min) / Math.log(d.max / d.min) : (v - d.min) / (d.max - d.min)
  return Math.max(0, Math.min(1, n))
}

function format(d, n) {
  var v = toValue(d, n)
  switch (d.fmt) {
  case "pct": return Math.round(v * 100) + "%"
  case "hz": return v >= 1000 ? (v / 1000).toFixed(2) + " kHz" : v.toFixed(1) + " Hz"
  case "ms": return v >= 1 ? v.toFixed(2) + " s" : Math.round(v * 1000) + " ms"
  case "st": return (v > 0 ? "+" : "") + Math.round(v) + " st"
  case "ct": return v.toFixed(0) + " ct"
  case "db": return v.toFixed(1) + " dB"
  case "x": return v.toFixed(2) + "x"
  case "int": return String(Math.round(v))
  case "voices": return Math.round(v) + "v"
  case "oct": return Math.round(v) + " oct"
  case "bits": return v.toFixed(1) + " bit"
  case "div": return "÷" + v.toFixed(1)
  case "ratio": return v.toFixed(1) + ":1"
  case "bool": return v >= 0.5 ? "on" : "off"
  case "choice": return d.choices[Math.round(v)]
  case "pan": return Math.abs(v) < 0.01 ? "C" : (v < 0 ? Math.round(-v * 100) + "L" : Math.round(v * 100) + "R")
  default: return v.toFixed(2)
  }
}

/** Step a normalized value: discrete params move one unit, others 1/50 (fine: 1/500). */
function stepped(d, n, dir, fine) {
  if (d.step) {
    var units = (d.max - d.min) / d.step
    return Math.max(0, Math.min(1, n + dir / units))
  }
  return Math.max(0, Math.min(1, n + dir * (fine ? 0.002 : 0.02)))
}

function defaults() {
  var out = {}
  for (var i = 0; i < order.length; i++) out[order[i]] = toNorm(params[order[i]], params[order[i]].def)
  return out
}

/** "Page › Section › Name" label for search. */
function paramPath(id) {
  for (var pi = 0; pi < PAGES.length; pi++)
    for (var si = 0; si < PAGES[pi].sections.length; si++)
      if (PAGES[pi].sections[si].items.indexOf(id) >= 0)
        return { page: pi, section: si, label: PAGES[pi].title + " › " + PAGES[pi].sections[si].title + " › " + params[id].name }
  return null
}

function moddableIds() {
  return order.filter(function(id) { return !!params[id].moddable })
}
