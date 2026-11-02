# soundgineer: the panel in a standalone desktop window (Qt 6 Quick, which
# Omarchy already ships for Quickshell). Built only when Qt 6 is found.
find_package(Qt6 6.4 COMPONENTS Quick Network QUIET)
if(NOT Qt6_FOUND)
  message(STATUS "Qt 6 Quick not found: skipping the soundgineer window app")
  return()
endif()

set(CMAKE_AUTOMOC ON)
qt_add_executable(soundgineer-app main.cpp)
set_target_properties(soundgineer-app PROPERTIES OUTPUT_NAME soundgineer)
qt_add_resources(soundgineer-app soundgineer_qml
  PREFIX /
  BASE ${PROJECT_SOURCE_DIR}
  FILES
    ${PROJECT_SOURCE_DIR}/app/Main.qml
    ${PROJECT_SOURCE_DIR}/plugin/ui/PanelBody.qml
    ${PROJECT_SOURCE_DIR}/plugin/ui/EngineClient.qml
    ${PROJECT_SOURCE_DIR}/plugin/ui/ItemRow.qml
    ${PROJECT_SOURCE_DIR}/plugin/ui/Picker.qml
    ${PROJECT_SOURCE_DIR}/plugin/ui/Model.js)
target_link_libraries(soundgineer-app PRIVATE Qt6::Quick Qt6::Network)
install(TARGETS soundgineer-app RUNTIME DESTINATION bin)
