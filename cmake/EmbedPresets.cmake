# Writes <out> with the factory presets as C++ string literals, one per line,
# for `const char* const presets[] = {
#include "factory_presets.inc"
# };`
function(sg_embed_factory_presets out)
  file(GLOB _presets ${PROJECT_SOURCE_DIR}/presets/factory/*.json)
  list(SORT _presets)
  set(_inc "")
  foreach(_p IN LISTS _presets)
    file(READ ${_p} _json)
    string(APPEND _inc "R\"SGJSON(${_json})SGJSON\",\n")
  endforeach()
  file(WRITE ${out} "${_inc}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_presets})
endfunction()
