cmake_minimum_required(VERSION 3.24)

project(soundgineer
  VERSION 0.1.0
  DESCRIPTION "Soundgineer for Omarchy: wavetable synth engine"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Build out of tree, e.g. cmake -B build. In-tree builds "
    "inside ~/.config/omarchy/plugins/ would make the shell hot-reload the plugin.")
endif()

option(SG_BUILD_ENGINE  "Build the soundgineer-engine daemon (needs the JACK API, e.g. pipewire-jack)" ON)
option(SG_BUILD_PLUGINS "Build VST3/LV2/Standalone targets with DPF (Phase 6)" OFF)
option(SG_BUILD_TESTS   "Build unit and parity tests" ON)

# Pure C++ DSP core and the dependency-free tools.
add_subdirectory(engine/core)
add_subdirectory(engine/tools)

if(SG_BUILD_ENGINE)
  add_subdirectory(engine/app)
endif()

if(SG_BUILD_PLUGINS)
  add_subdirectory(engine/plugins)
endif()

if(SG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
