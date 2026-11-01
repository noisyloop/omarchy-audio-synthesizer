# Command-line tools: sg_core only, no audio-system dependency.
add_executable(sg-render sg_render.cpp)
target_link_libraries(sg-render PRIVATE sg_core)

add_executable(sg-ctl sg_ctl.cpp)
target_link_libraries(sg-ctl PRIVATE sg_core)

install(TARGETS sg-render sg-ctl RUNTIME DESTINATION bin)
