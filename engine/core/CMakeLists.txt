# sg_core: the DSP core. Plain C++20, no JUCE, no UI, no I/O. Every target
# (daemon, sg-render, VST3/LV2/Standalone) links this one library.
add_library(sg_core STATIC
  src/version.cpp)

target_include_directories(sg_core PUBLIC include)
target_compile_features(sg_core PUBLIC cxx_std_20)
target_compile_definitions(sg_core PUBLIC
  SG_VERSION_STRING="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sg_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
  # Parity with the reference depends on IEEE semantics: never let the
  # compiler reassociate or contract floating-point math in the core.
  target_compile_options(sg_core PUBLIC -fno-fast-math -ffp-contract=off)
endif()
