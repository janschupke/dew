# ==============================================================================
# dew_tests - Catch2, registered with CTest so `ctest` is the single command
# that runs everything.
# ==============================================================================

add_executable(dew_tests
  BuildInfoTests.cpp
  UiSmokeTests.cpp
)

target_link_libraries(dew_tests
  PRIVATE
    dew_core
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

catch_discover_tests(dew_tests)
