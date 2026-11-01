# soundgineer-engine: the headless daemon the Omarchy plugin talks to.
# Audio and MIDI go through the JACK API. On Omarchy that is pipewire-jack
# (MIT), so the engine is a native PipeWire client and sees ALSA/USB MIDI
# devices as JACK MIDI ports through PipeWire's MIDI bridge.
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)

add_executable(soundgineer-engine engine_main.cpp)
target_link_libraries(soundgineer-engine PRIVATE sg_core PkgConfig::JACK)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(soundgineer-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS soundgineer-engine RUNTIME DESTINATION bin)
