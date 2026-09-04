#include <catch2/catch_test_macros.hpp>

#include "model/BuildInfo.h"

namespace
{

/** The build system, as text.

    The version is declared in CMakeLists.txt and reaches the binary as a
    compile definition, so a test that spelled the expected version itself would
    be a second home for it - and every release would be a two-file edit with
    the suite red in between. It reads the declaration instead.

    Comment lines are dropped before anything is matched. The gates in this tree
    have been fooled by their own explanations before, and the lines above the
    two declarations below talk about version numbers.
*/
juce::StringArray declarationLines()
{
    const juce::File root { DEW_REPO_DIR };
    const auto text = root.getChildFile ("CMakeLists.txt").loadFileAsString();

    REQUIRE (text.isNotEmpty());

    juce::StringArray lines;
    lines.addLines (text);

    juce::StringArray code;

    for (const auto& line : lines)
        if (! line.trim().startsWith ("#"))
            code.add (line.trim());

    return code;
}

/** The value of a `set(<name> "<value>" ...)` call, or a marker that says the
    call was not found - which must fail loudly rather than read as an empty
    suffix, because an empty suffix is what a release looks like.
*/
juce::String settingNamed (const juce::StringArray& code, const juce::String& name)
{
    const auto prefix = "set(" + name + " \"";

    for (const auto& line : code)
        if (line.startsWith (prefix))
            return line.fromFirstOccurrenceOf (prefix, false, false)
                .upToFirstOccurrenceOf ("\"", false, false);

    return "<not declared>";
}

} // namespace

TEST_CASE ("build reports the version CMake was told", "[buildinfo]")
{
    const auto code = declarationLines();

    juce::String numeric;

    for (const auto& line : code)
        if (line.startsWith ("project(dew VERSION "))
            numeric = line.fromFirstOccurrenceOf ("VERSION ", false, false)
                          .upToFirstOccurrenceOf (" ", false, false);

    // A SemVer triple, and the control case for the reader above: a parse that
    // silently found nothing would make every assertion below vacuous.
    REQUIRE (numeric.isNotEmpty());
    REQUIRE (juce::StringArray::fromTokens (numeric, ".", "").size() == 3);

    const auto suffix = settingNamed (code, "DEW_VERSION_SUFFIX");

    REQUIRE (suffix != "<not declared>");
    REQUIRE (dew::BuildInfo::version() == numeric + suffix);
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
