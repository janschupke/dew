#include <catch2/catch_test_macros.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/OfflineRenderer.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

using namespace dew;

TEST_CASE ("every demo is embedded, loads clean, and is audible", "[demos]")
{
    const auto& demos = ProjectFactory::demos();
    REQUIRE (demos.size() >= 4);

    for (int i = 0; i < (int) demos.size(); ++i)
    {
        const auto& demo = demos[(size_t) i];
        INFO ("demo: " << demo.menuName << " (" << demo.fileName << ")");

        juce::StringArray warnings;
        const auto project = DemoLibrary::load (i, warnings);

        // Embedded at all, and free of the "refers to something missing"
        // warnings that mean a demo was written against an older schema.
        REQUIRE (project.isValid());
        INFO ("warnings: " << warnings.joinIntoString (" | "));
        REQUIRE (warnings.isEmpty());

        juce::AudioBuffer<float> rendered;
        const auto report = OfflineRenderer::renderToBuffer (project, rendered);

        REQUIRE (report.ok());
        REQUIRE (report.warnings.isEmpty());

        // Audible, and not clipping: a demo that distorts is a bad first
        // impression, and the effects demo peaked at 1.77 before its levels
        // were brought down.
        INFO ("peak " << report.peak << " rms " << report.rms);
        REQUIRE (report.peak > 0.05f);
        REQUIRE (report.peak <= 1.0f);
        REQUIRE (report.rms > 0.01f);
    }
}

TEST_CASE ("the embedded demos match the committed files", "[demos]")
{
    // The files under examples/ are what CI renders and what a reader looks at;
    // the binary is what the menu opens. If they drift, one of them is a lie.
    const juce::File examples (DEW_EXAMPLES_DIR);

    for (const auto& demo : ProjectFactory::demos())
    {
        const auto file = examples.getChildFile (demo.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());
        REQUIRE (DemoLibrary::jsonFor (demo.fileName).trim() == file.loadFileAsString().trim());
    }
}

TEST_CASE ("the committed demo files match what the factory builds", "[demos]")
{
    // Regenerate with `dew_render --write-demos examples` when this fails.
    const juce::File examples (DEW_EXAMPLES_DIR);

    for (const auto& demo : ProjectFactory::demos())
    {
        INFO ("demo: " << demo.menuName);

        const auto loaded = ProjectSerializer::readFromFile (examples.getChildFile (demo.fileName));

        REQUIRE (loaded.ok());
        REQUIRE (loaded.warnings.isEmpty());
        REQUIRE (loaded.tree.isEquivalentTo (demo.build()));
    }
}

TEST_CASE ("each demo exercises the part of the app it is named for", "[demos]")
{
    // Otherwise a "demo library" is four copies of the same project.
    juce::StringArray warnings;

    const auto countIn = [] (const juce::ValueTree& tree, const juce::Identifier& type)
    {
        int n = 0;

        for (const auto& child : tree)
            if (child.hasType (type))
                ++n;

        return n;
    };

    const auto& demos = ProjectFactory::demos();

    const auto indexOfFile = [&demos] (const juce::String& fileName)
    {
        for (int i = 0; i < (int) demos.size(); ++i)
            if (fileName == demos[(size_t) i].fileName)
                return i;

        return -1;
    };

    SECTION ("the piano roll demo has chords and varied note lengths")
    {
        const auto project = DemoLibrary::load (indexOfFile ("melody.dew"), warnings);
        const auto pattern = project.getChildWithName (ids::PATTERN);

        juce::Array<int> lengths;
        int simultaneous = 0;

        for (const auto& note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            lengths.addIfNotAlreadyThere ((int) note[ids::lengthSteps]);
        }

        // Three notes starting on the same step is the thing a step grid
        // cannot express.
        for (const auto& note : pattern)
        {
            if (! note.hasType (ids::NOTE))
                continue;

            int here = 0;

            for (const auto& other : pattern)
                if (other.hasType (ids::NOTE) && (int) other[ids::ch] == (int) note[ids::ch]
                    && (int) other[ids::step] == (int) note[ids::step])
                    ++here;

            simultaneous = juce::jmax (simultaneous, here);
        }

        REQUIRE (lengths.size() >= 4);
        REQUIRE (simultaneous >= 3);
    }

    SECTION ("the effects demo uses chains on both a channel and a mixer track")
    {
        const auto project = DemoLibrary::load (indexOfFile ("effects.dew"), warnings);

        int channelEffects = 0, trackEffects = 0;
        juce::StringArray types;

        for (const auto& channel : project)
            if (channel.hasType (ids::CHANNEL))
            {
                channelEffects += countIn (channel, ids::EFFECT);

                for (const auto& effect : channel)
                    if (effect.hasType (ids::EFFECT))
                        types.addIfNotAlreadyThere (effect[ids::type].toString());
            }

        for (const auto& track : project.getChildWithName (ids::MIXER))
            if (track.hasType (ids::MIXER_TRACK))
            {
                trackEffects += countIn (track, ids::EFFECT);

                for (const auto& effect : track)
                    if (effect.hasType (ids::EFFECT))
                        types.addIfNotAlreadyThere (effect[ids::type].toString());
            }

        REQUIRE (channelEffects > 0);
        REQUIRE (trackEffects > 0);
        REQUIRE (types.size() >= 5);
    }

    SECTION ("the automation demo has curves that are actually placed")
    {
        const auto project = DemoLibrary::load (indexOfFile ("automation.dew"), warnings);

        REQUIRE (countIn (project, ids::AUTOMATION) >= 2);

        int automationClips = 0;

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            for (const auto& clip : track)
                if (clip.hasType (ids::CLIP) && clip[ids::kind].toString() == "automation")
                    ++automationClips;

        REQUIRE (automationClips >= 2);

        // And the engine agrees they will do something.
        const auto snapshot = buildSnapshot (project, nullptr);
        REQUIRE (snapshot.anyAutomation);
        REQUIRE (snapshot.anyEffects);
    }
}
