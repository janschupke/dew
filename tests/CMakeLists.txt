# ==============================================================================
# dew_tests - Catch2, registered with CTest so `ctest` is the single command
# that runs everything.
# ==============================================================================

add_executable(dew_tests
  BuildInfoTests.cpp
  DocumentIdentityTests.cpp
  DemoLibraryTests.cpp
  DesignSystemTests.cpp
  SchemaTests.cpp
  SettingsTests.cpp
  SelectionTests.cpp
  StatusBarTests.cpp
  ProjectEditsTests.cpp
  NoteToolsTests.cpp
  TransportTests.cpp
  SequencerTests.cpp
  LoopRegionTests.cpp
  StepGridTests.cpp
  PreviewQueueTests.cpp
  SignalTapTests.cpp
  SnapshotBridgeTests.cpp
  AudioSettingsTests.cpp
  MidiRouterTests.cpp
  MidiInputTests.cpp
  PitchBendTests.cpp
  AutomationTests.cpp
  EffectTests.cpp
  EngineTests.cpp
  MixerTests.cpp
  OscillatorTests.cpp
  TimelineViewTests.cpp
  TimelineRulerTests.cpp
  TypographyTests.cpp
  PianoRollTests.cpp
  PianoRollToolsTests.cpp
  PlaylistTests.cpp
  MidiExportTests.cpp
  RenderFormatTests.cpp
  RenderJobTests.cpp
  RenderPanelTests.cpp
  RenderPostTests.cpp
  RenderStemTests.cpp
  RenderRangeTests.cpp
  UiSmokeTests.cpp
  SignalScopeTests.cpp
)

target_link_libraries(dew_tests
  PRIVATE
    dew_core
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# The example project is committed, so a test can check it stays loadable and
# in step with the factory that generates it.
# The typography gate scans the sources for raw font construction, so it needs
# to know where they are. Same idiom as the examples directory above.
target_compile_definitions(dew_tests PRIVATE
  DEW_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
  DEW_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")

catch_discover_tests(dew_tests)
