# VST3 + LV2 + JACK standalone from the same sg_core, via DPF.
include(${PROJECT_SOURCE_DIR}/cmake/DPF.cmake)

# Embed the factory presets as programs.
include(${PROJECT_SOURCE_DIR}/cmake/EmbedPresets.cmake)
sg_embed_factory_presets(${CMAKE_CURRENT_BINARY_DIR}/factory_presets.inc)

dpf_add_plugin(soundgineer
  TARGETS jack lv2 vst3
  FILES_DSP SoundgineerPlugin.cpp)

target_include_directories(soundgineer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(soundgineer PUBLIC sg_core)
