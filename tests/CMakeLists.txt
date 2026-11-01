# Unit tests: plain C++ with a tiny check macro, no test framework dependency.
add_executable(sg_core_tests core_tests.cpp)
target_link_libraries(sg_core_tests PRIVATE sg_core)
add_test(NAME core COMMAND sg_core_tests)

add_test(NAME sg-render-version COMMAND sg-render --version)
add_test(NAME sg-ctl-version COMMAND sg-ctl --version)
if(TARGET soundgineer-engine)
  add_test(NAME engine-version COMMAND soundgineer-engine --version)
endif()

# Parity tests (reference golden WAVs vs sg-render) are added here in Phase 3.
