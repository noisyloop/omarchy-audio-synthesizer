# DPF (DISTRHO Plugin Framework, ISC licence) builds the VST3, LV2 and JACK
# standalone targets. Fetched at configure time only when SG_BUILD_PLUGINS is
# on; offline / packaged builds pass -DFETCHCONTENT_SOURCE_DIR_DPF=/path/to/DPF.
# DPF has no release tags; this pins a commit from 2025-10-23.
include(FetchContent)
FetchContent_Declare(DPF
  GIT_REPOSITORY https://github.com/DISTRHO/DPF.git
  GIT_TAG        4238e1c7f0351bbe488d79f0899c540543ac7583
  GIT_SUBMODULES "")
FetchContent_GetProperties(DPF)
if(NOT dpf_POPULATED)
  FetchContent_Populate(DPF)
  add_subdirectory(${dpf_SOURCE_DIR} ${dpf_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
