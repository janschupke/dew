#pragma once

#include <string>

#include <juce_data_structures/juce_data_structures.h>

#include "lang/Compile.h"
#include "model/Ids.h"

/** Compiling a score and finding what it wrote into the document.

    Shared by the bake tests and the recompile tests, which is the point: a
    recompile has to find exactly what the first bake produced, so both sides
    look for it the same way - by generated id, never by position.
*/
namespace dew::testing
{

inline std::string exampleSource()
{
    const juce::File file { juce::String (DEW_EXAMPLES_DIR) + "/amber.score" };
    REQUIRE (file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

inline lang::Score compileOrFail (const std::string& source)
{
    const auto result = lang::compile (source, "test.score");
    INFO (result.report (source, "test.score"));
    REQUIRE (result.ok());
    return *result.score;
}

inline int countChildren (const juce::ValueTree& tree, const juce::Identifier& type)
{
    auto count = 0;

    for (const auto& child : tree)
        if (child.hasType (type))
            ++count;

    return count;
}

inline juce::ValueTree generatedLane (const juce::ValueTree& project)
{
    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
        if (track.hasType (ids::PLAYLIST_TRACK)
            && track[ids::name].toString() == ScoreBake::generatedTrackName())
            return track;

    return {};
}

/** The pattern a compile wrote under this identity, or an invalid tree. */
inline juce::ValueTree findByGenId (const juce::ValueTree& project, const juce::String& id)
{
    for (const auto& pattern : project)
        if (pattern.hasType (ids::PATTERN) && pattern[ids::genId].toString() == id)
            return pattern;

    return {};
}

/** The first pattern a compile wrote, whatever it is called. */
inline juce::ValueTree generatedPattern (const juce::ValueTree& project)
{
    for (const auto& pattern : project)
        if (pattern.hasType (ids::PATTERN) && pattern[ids::genId].toString().isNotEmpty())
            return pattern;

    return {};
}

/** A score whose repeats really do differ.

    tinyScore has nothing random in it, so `verse x2` renders the same notes
    twice and de-duplicates to ONE pattern - correct, and useless for testing
    what happens to a second one.
*/
inline std::string varyingScore (const std::string& arrangement)
{
    return "song {\n  tempo 120\n  meter 4/4\n  key C major\n  seed 99\n}\n"
           "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
           "rhythm pulse { 1/4 }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n  length 4 bars\n  harmony h\n"
           "  part lead {\n    melody {\n      rhythm pulse\n      variance 0.9\n    }\n  }\n}\n"
           "arrangement {\n"
           + arrangement + "}\n";
}

/** A minimal score, so a test can vary one thing without a long fixture. */
inline std::string tinyScore (const std::string& body = {})
{
    return "song {\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "}\n"
           "channel pad { mixer 1 }\n"
           "voicing warm { size 3 voices }\n"
           "rhythm held { 1/1 }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n"
           "  length 4 bars\n"
           "  harmony h\n"
           "  part pad {\n"
           "    chords with warm\n"
           "    rhythm held\n"
           "  }\n"
           "}\n"
           + (body.empty() ? std::string ("arrangement {\n  verse\n}\n") : body);
}

} // namespace dew::testing
