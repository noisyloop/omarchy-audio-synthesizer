# VST3 + LV2 + JACK standalone from the same sg_core, via DPF.
include(${PROJECT_SOURCE_DIR}/cmake/DPF.cmake)

# Embed the factory presets as programs.
file(GLOB _sg_presets ${PROJECT_SOURCE_DIR}/presets/factory/*.json)
list(SORT _sg_presets)
set(_sg_inc "")
foreach(_p IN LISTS _sg_presets)
  file(READ ${_p} _json)
  string(APPEND _sg_inc "R\"SGJSON(${_json})SGJSON\",\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/factory_presets.inc "${_sg_inc}")

dpf_add_plugin(soundgineer
  TARGETS jack lv2 vst3
  FILES_DSP SoundgineerPlugin.cpp)

target_include_directories(soundgineer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(soundgineer PUBLIC sg_core)
