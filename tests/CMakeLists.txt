# Unit tests: plain C++ with a tiny check macro, no test framework dependency.
add_executable(sg_core_tests core_tests.cpp)
target_link_libraries(sg_core_tests PRIVATE sg_core)
target_compile_definitions(sg_core_tests PRIVATE SG_PRESET_DIR="${PROJECT_SOURCE_DIR}/presets/factory")
add_test(NAME core COMMAND sg_core_tests)

add_test(NAME sg-render-version COMMAND sg-render --version)
add_test(NAME sg-ctl-version COMMAND sg-ctl --version)
if(TARGET soundgineer-engine)
  add_test(NAME engine-version COMMAND soundgineer-engine --version)
endif()


add_test(NAME parity
  COMMAND sg-render --parity ${PROJECT_SOURCE_DIR}/tests/parity/cases.json ${PROJECT_SOURCE_DIR}/tests/parity/fixtures)
set_tests_properties(parity PROPERTIES ENVIRONMENT "SG_FACTORY_PRESETS=${PROJECT_SOURCE_DIR}/presets/factory")
