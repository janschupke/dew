#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "SourceScan.h"
#include "model/AutomationTargets.h"
#include "model/ModuleCatalog.h"

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

TEST_CASE ("every compiled source is one the gates can see", "[build][gate]")
{
    // The compiled-sources file is the six libraries' own SOURCES lists, written
    // out by CMake at generate time. DEW_SOURCE_DIR is a directory that is merely named src.
    //
    // They agree today. The day they stop - a layer moved out, a generated file
    // compiled from the build tree - every gate built on the directory walk
    // silently stops covering it and keeps reporting success. This is the test
    // that refuses to let that be quiet.
    const juce::File list { DEW_COMPILED_SOURCES_FILE };
    REQUIRE (list.existsAsFile());

    juce::StringArray compiled;
    compiled.addLines (list.loadFileAsString());
    compiled.removeEmptyStrings();

    juce::StringArray walked;

    for (const auto& f : sourceFiles())
        walked.add (f.getFileName());

    juce::StringArray missing;
    auto ours = 0;

    for (const auto& source : compiled)
    {
        // JUCE puts its own module sources into every target that links a
        // module, and juce_add_binary_data generates into the build tree. Both
        // arrive here as ABSOLUTE paths; dew's own sources are listed relative
        // to src/, which is exactly the distinction we want.
        if (juce::File::isAbsolutePath (source))
            continue;

        ++ours;

        const auto name = source.fromLastOccurrenceOf ("/", false, false);

        if (! walked.contains (name))
            missing.add (source);
    }

    INFO ("dew sources found in the libraries: " << ours);
    CHECK (ours > 60);

    INFO ("compiled but not seen by the source gates:\n" << missing.joinIntoString ("\n"));
    CHECK (missing.isEmpty());
}

TEST_CASE ("every layer is represented in the scanned sources", "[build][gate]")
{
    // Named directories rather than a count, so moving one layer out cannot be
    // masked by another growing.
    for (const auto* layer : { "model", "engine", "io", "ui", "app" })
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

TEST_CASE ("the engine layer opens no files and no devices", "[build][layering]")
{
    // The property the io split exists to create, stated as a test rather than
    // left to whoever edits src/CMakeLists.txt next.
    //
    // An engine that cannot reach a device or the filesystem is one the whole
    // test suite can drive with neither - which is why 633 tests run in CI with
    // no audio hardware - and it is the precondition for ever wrapping this
    // engine as a plugin, since a plugin must not go looking at the filesystem
    // on its host's behalf.
    const auto found = offenders ([] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        if (! trimmed.startsWith ("#include"))
            return false;

        return trimmed.contains ("juce_audio_devices")
               || trimmed.contains ("juce_audio_formats")
               || trimmed.contains ("\"io/");
    });

    juce::StringArray fromEngine;

    for (const auto& offender : found)
    {
        // offenders() reports by file NAME, so ask the walk where it lives.
        for (const auto& f : sourceFiles())
            if (offender.startsWith (f.getFileName() + ":")
                && f.getParentDirectory().getFileName() == "engine")
                fromEngine.add (offender);
    }

    INFO ("engine sources reaching for a device or a file:\n" << fromEngine.joinIntoString ("\n"));
    CHECK (fromEngine.isEmpty());
}

TEST_CASE ("no source spells an automatable parameter as a string literal", "[build][gate]")
{
    // The twenty names that were the actual defect: the snapshot builder held a
    // table mapping "cutoff", "roomSize", "midFreq" and the rest to an enum, and
    // it was the only place in production that wrote a property name by hand.
    //
    // The failure was silent in the worst way. Renaming an identifier in Ids.h
    // compiled cleanly, the schema and the editor followed the new name, and
    // every automation curve pointing at the old one simply stopped doing
    // anything - because a name that matches nothing is indistinguishable from
    // an automation of nothing.
    //
    // Scoped to automatable parameters rather than every identifier, and taken
    // from the catalog rather than scraped, because dew's identifiers also
    // include node types (CHANNEL, MIXER) that are legitimate UI captions, and
    // property names that are also legitimate VALUES - "loop", "record",
    // "wavetable", "drive". Widening this beyond the parameters would be a gate
    // that cries wolf, which is a gate people turn off.
    juce::StringArray names;

    const auto collect = [&names] (const std::vector<dew::AutomationParamSpec>& specs)
    {
        for (const auto& spec : specs)
            if (spec.property != nullptr)
                names.addIfNotAlreadyThere (spec.property->toString());
    };

    collect (dew::channelParams());
    collect (dew::mixerTrackParams());
    collect (dew::masterParams());
    collect (dew::oscParams());

    for (const auto& descriptor : dew::effectDescriptors())
        collect (dew::effectParams (descriptor.id));

    // An effect's id and one of its parameters share a spelling in one case -
    // "drive" is both - and the id is a value a file legitimately contains.
    for (const auto& descriptor : dew::effectDescriptors())
        names.removeString (descriptor.id);

    // Control case: a gate over an empty list is not a gate.
    REQUIRE (names.size() > 15);
    REQUIRE (names.contains ("cutoff"));
    REQUIRE (names.contains ("midFreq"));

    const auto found = dew::testing::offenders ([&names] (const juce::String& line)
    {
        const auto trimmed = line.trim();

        // Doc comments name properties all through this codebase, deliberately.
        if (trimmed.startsWith ("//") || trimmed.startsWith ("*") || trimmed.startsWith ("/*"))
            return false;

        for (const auto& name : names)
            if (line.contains ("\"" + name + "\""))
                return true;

        return false;
    }, { "Ids.h" });

    INFO ("automatable parameters written as string literals:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}
