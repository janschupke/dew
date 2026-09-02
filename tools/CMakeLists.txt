# ==============================================================================
# dew_render - offline renderer. Loads a .dew project and writes a WAV without
# touching an audio device, which is how playback gets verified in CI and
# without ears. Real rendering lands in phase 3; phase 1 proves the console-app
# target links dew_core.
# ==============================================================================

juce_add_console_app(dew_render
  PRODUCT_NAME "dew_render"
  VERSION      "${PROJECT_VERSION}"
)

target_sources(dew_render PRIVATE render_main.cpp)

target_link_libraries(dew_render
  PRIVATE
    dew_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
