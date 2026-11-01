# JUCE is fetched at configure time only; nothing in the built programs talks
# to the network. Offline / packaged builds point CMake at a local copy with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#
# Pinned: JUCE 9.0.4 (tag commit 3fb5cbc0340758596dac39f7c8de47d562b4896e).
# Licence: JUCE modules are AGPLv3 or commercial. See docs/porting-notes.md
# ("Licensing") before distributing binaries.
include(FetchContent)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        9.0.4
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   FALSE)

FetchContent_MakeAvailable(JUCE)

# Settings shared by every JUCE target in this project.
add_library(sg_juce_options INTERFACE)
target_compile_definitions(sg_juce_options INTERFACE
  JUCE_USE_CURL=0                 # no network code linked in
  JUCE_WEB_BROWSER=0
  JUCE_MODAL_LOOPS_PERMITTED=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1
  JUCE_JACK=1                     # PipeWire through pipewire-jack
  JUCE_ALSA=1)                    # ALSA sequencer MIDI
