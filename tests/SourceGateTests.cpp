#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "SourceScan.h"

using namespace dew::testing;

TEST_CASE ("the source gates can see every source directory", "[build][gate]")
{
    // Several tests enforce a convention by scanning DEW_SOURCE_DIR - no raw
    // font construction today, and more to come. Every one of them is silent
    // when it finds nothing, which is indistinguishable from finding nothing
    // because it was looking in the wrong place.
    //
    // So: a restructure that moves production code out of src/ must fail HERE,
    // loudly, rather than turning every other gate into a no-op that still
    // reports success.
    const auto files = sourceFiles();

    REQUIRE (files.size() > 90);

    for (const auto* expected : { "Tokens.h", "DewControls.cpp", "PianoRollComponent.cpp",
                                  "AudioEngine.cpp", "ProjectSchema.cpp", "Settings.cpp",
                                  "Effects.cpp", "MainComponent.cpp" })
    {
        INFO ("expected a file named " << expected << " under DEW_SOURCE_DIR");
        REQUIRE (std::any_of (files.begin(), files.end(),
                              [expected] (const juce::File& f) { return f.getFileName() == expected; }));
    }
}

TEST_CASE ("every layer is represented in the scanned sources", "[build][gate]")
{
    // Named directories rather than a count, so moving one layer out cannot be
    // masked by another growing.
    for (const auto* layer : { "model", "engine", "ui", "app" })
    {
        auto seen = false;

        for (const auto& f : sourceFiles())
            if (f.getParentDirectory().getFileName() == layer
                || f.getFullPathName().contains (juce::String ("/") + layer + "/"))
                seen = true;

        INFO ("no scanned source lives under src/" << layer);
        REQUIRE (seen);
    }
}

TEST_CASE ("no source file includes another layer by relative path", "[build][gate]")
{
    // Rooted includes only: "model/Ids.h", never "../model/Ids.h".
    //
    // A relative-parent include hard-codes a file's position in the tree, which
    // is exactly what moving a layer changes - so a hundred of them are a
    // hundred edits standing between the source tree and any restructure.
    // src/ is already on the include path, so the rooted form has always
    // worked; tools/ and tests/ have always used it.
    const auto found = offenders ([] (const juce::String& line)
                                  { return line.trim().startsWith ("#include \"../"); });

    INFO ("relative-parent includes:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
