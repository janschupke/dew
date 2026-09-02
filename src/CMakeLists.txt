# ==============================================================================
# dew_core - model + engine, shared by the GUI app, the renderer and the tests.
# dew      - the GUI application.
# ==============================================================================

add_library(dew_core STATIC
  BuildInfo.cpp
  ui/MainComponent.cpp
)

target_include_directories(dew_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(dew_core
  PUBLIC
    juce::juce_audio_utils      # pulls audio_basics / devices / formats / processors
    juce::juce_dsp
    juce::juce_data_structures  # ValueTree, UndoManager
    juce::juce_gui_extra        # FileBasedDocument
  PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(dew_core
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Set by juce_add_gui_app / juce_add_console_app on THOSE targets only. A
    # static library shared between them never sees it, and JUCE's headers
    # behave differently without it, so it has to be declared here by hand.
    JUCE_STANDALONE_APPLICATION=1
    DEW_VERSION_STRING="${PROJECT_VERSION}"
    DEW_JUCE_PIN="${DEW_PIN_JUCE}"
)

# ------------------------------------------------------------------------------

juce_add_gui_app(dew
  PRODUCT_NAME              "dew"
  COMPANY_NAME              "dew"
  BUNDLE_ID                 "io.dew.app"
  VERSION                   "${PROJECT_VERSION}"
  MICROPHONE_PERMISSION_ENABLED FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  DOCUMENT_EXTENSIONS       "dew"
)

target_sources(dew PRIVATE
  main.cpp
)

target_link_libraries(dew
  PRIVATE
    dew_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
