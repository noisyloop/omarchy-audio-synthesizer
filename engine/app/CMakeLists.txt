# soundgineer-engine: the headless daemon the Omarchy plugin talks to.
# Only non-GUI JUCE modules, so no X11/Wayland/freetype headers are needed.
juce_add_console_app(soundgineer-engine
  PRODUCT_NAME "soundgineer-engine")

target_sources(soundgineer-engine PRIVATE engine_main.cpp)

target_link_libraries(soundgineer-engine PRIVATE
  sg_core
  sg_juce_options
  juce::juce_core
  juce::juce_events
  juce::juce_audio_basics
  juce::juce_audio_devices
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

install(TARGETS soundgineer-engine RUNTIME DESTINATION bin)
