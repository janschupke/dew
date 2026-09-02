# ==============================================================================
# dew_tests - Catch2, registered with CTest so `ctest` is the single command
# that runs everything.
# ==============================================================================

add_executable(dew_tests
  BuildInfoTests.cpp
  DocumentIdentityTests.cpp
  DesignSystemTests.cpp
  SchemaTests.cpp
  ProjectEditsTests.cpp
  TransportTests.cpp
  SequencerTests.cpp
  SnapshotBridgeTests.cpp
  EffectTests.cpp
  EngineTests.cpp
  MixerTests.cpp
  TimelineViewTests.cpp
  PianoRollTests.cpp
  PlaylistTests.cpp
  UiSmokeTests.cpp
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
target_compile_definitions(dew_tests PRIVATE
  DEW_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

catch_discover_tests(dew_tests)
