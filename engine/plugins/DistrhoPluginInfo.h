#pragma once

#define DISTRHO_PLUGIN_BRAND   "Soundgineer"
#define DISTRHO_PLUGIN_NAME    "Soundgineer"
#define DISTRHO_PLUGIN_URI     "urn:soundgineer:synth"
#define DISTRHO_PLUGIN_CLAP_ID "soundgineer.synth"

#define DISTRHO_PLUGIN_BRAND_ID  Sgnr
#define DISTRHO_PLUGIN_UNIQUE_ID Sgsy

#define DISTRHO_PLUGIN_HAS_UI           0
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0
#define DISTRHO_PLUGIN_WANT_PROGRAMS    1
// Mod matrix, LFO shapes and FX order travel as one JSON state string, so
// host sessions and LV2 presets keep them (parameters alone cannot).
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
