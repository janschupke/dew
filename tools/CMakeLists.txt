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

# ==============================================================================
# dew_score - compiles a .score file into a .dew project. The language's
# headless verification path: text -> project -> dew_render -> WAV, all in CI.
# ==============================================================================

juce_add_console_app(dew_score
  PRODUCT_NAME "dew_score"
  VERSION      "${PROJECT_VERSION}"
)

target_sources(dew_score PRIVATE score_main.cpp)

target_link_libraries(dew_score
  PRIVATE
    dew_model
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ==============================================================================
# dew_docs - writes the score language's schema as JSON, for the website.
#
# A plain add_executable, and dew_lang alone on the link line. dew_lang links
# nothing at all, JUCE included, so this tool does not either - which makes it a
# second place that claim is proved rather than merely asserted, and lets the
# lang-linux job build it under gcc and libstdc++ for free.
#
# It is deliberately NOT a mode on dew_score. The design tokens live in
# dew_design, which PUBLIC-links dew_engine; one tool emitting both artefacts
# would put juce_gui_basics on the link line of the score compiler, which is the
# mistake dew_render's header comment above already records once. The tokens are
# emitted by dew_shot, which links dew_ui already and whose job is showing the
# design system.
# ==============================================================================

add_executable(dew_docs docs_main.cpp)

target_link_libraries(dew_docs
  PRIVATE
    dew_lang
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
