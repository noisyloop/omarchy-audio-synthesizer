# sg_core: the DSP core. Plain C++20, no third-party code, no UI, no I/O on
# the audio path. Every target (daemon, sg-render, VST3/LV2/Standalone)
# links this one library.
add_library(sg_core STATIC
  src/version.cpp
  src/params.cpp
  src/fft.cpp
  src/wavetable_gen.cpp
  src/dsp.cpp
  src/voice.cpp
  src/effects.cpp
  src/processor.cpp
  src/json.cpp
  src/patch.cpp
  src/render.cpp)

target_include_directories(sg_core PUBLIC include)
target_compile_features(sg_core PUBLIC cxx_std_20)
target_compile_definitions(sg_core PUBLIC SG_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(sg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sg_core PRIVATE -Wall -Wextra)
  # Parity with the reference depends on IEEE semantics: never let the
  # compiler reassociate or fuse floating-point operations in the core.
  target_compile_options(sg_core PUBLIC -fno-fast-math -ffp-contract=off)
endif()
