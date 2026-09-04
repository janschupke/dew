#include <catch2/catch_test_macros.hpp>

#include "model/BuildInfo.h"

TEST_CASE ("build reports the version CMake was told", "[buildinfo]")
{
    REQUIRE (dew::BuildInfo::version() == juce::String ("0.1.0"));
}

TEST_CASE ("build records the JUCE commit it was pinned to", "[buildinfo][deps]")
{
    const auto pin = dew::BuildInfo::jucePin();

    // A 40-hex commit, not a tag or a branch - the whole point of the pin.
    REQUIRE (pin.length() == 40);
    REQUIRE (pin.containsOnly ("0123456789abcdef"));
}

TEST_CASE ("the JUCE headers in this binary are the pinned major version", "[buildinfo][deps]")
{
    REQUIRE (JUCE_MAJOR_VERSION == 9);
}
