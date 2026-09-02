# ==============================================================================
# dew_render - offline renderer. Loads a .dew project and writes a WAV without
# touching an audio device, which is how playback gets verified in CI and
# without ears.
#
# It links dew_io and stops there. It used to link dew_core, which meant this
# headless tool compiled and linked all thirty UI translation units and the
# whole of juce_gui_extra to write a WAV.
# ==============================================================================

juce_add_console_app(dew_render
  PRODUCT_NAME "dew_render"
  VERSION      "${PROJECT_VERSION}"
)

target_sources(dew_render PRIVATE render_main.cpp)

target_link_libraries(dew_render
  PRIVATE
    dew_io
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ==============================================================================
# dew_shot - renders the UI to PNG offscreen. The only way to see the interface
# in a headless session, in CI, or without screen-recording permission.
# ==============================================================================

juce_add_console_app(dew_shot
  PRODUCT_NAME "dew_shot"
  VERSION      "${PROJECT_VERSION}"
)

target_sources(dew_shot PRIVATE shot_main.cpp)

target_link_libraries(dew_shot
  PRIVATE
    dew_ui
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
