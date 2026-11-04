# Command-line tools: sg_core only, no audio-system dependency.
add_executable(sg-render sg_render.cpp)
target_link_libraries(sg-render PRIVATE sg_core)

add_executable(sg-ctl sg_ctl.cpp)
target_link_libraries(sg-ctl PRIVATE sg_core)

install(TARGETS sg-render sg-ctl RUNTIME DESTINATION bin)

# sg-pad: game controller -> engine socket. Linux evdev only (kernel headers).
add_library(sg_padmap STATIC pad_map.cpp)
target_include_directories(sg_padmap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sg_padmap PUBLIC sg_core)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(sg-pad sg_pad.cpp)
  target_link_libraries(sg-pad PRIVATE sg_padmap)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sg-pad PRIVATE -Wall -Wextra)
    target_compile_options(sg_padmap PRIVATE -Wall -Wextra)
  endif()
  install(TARGETS sg-pad RUNTIME DESTINATION bin)
endif()
