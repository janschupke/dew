#include <catch2/catch_test_macros.hpp>

#include "engine/EngineSnapshot.h"
#include "i18n/Strings.h"
#include "io/OfflineRenderer.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"

using namespace dew;

TEST_CASE ("every demo is embedded, loads clean, and is audible", "[demos]")
{
    const auto& demos = ProjectFactory::demos();
    REQUIRE (demos.size() >= 4);

    for (int i = 0; i < (int) demos.size(); ++i)
    {
        const auto& demo = demos[(size_t) i];
        INFO ("demo: " << keyOf (demo.menuName) << " (" << demo.fileName << ")");

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
        INFO ("demo: " << keyOf (demo.menuName));

        const auto loaded = ProjectSerializer::readFromFile (examples.getChildFile (demo.fileName));

        REQUIRE (loaded.ok());
        REQUIRE (loaded.warnings.isEmpty());
        REQUIRE (loaded.tree.isEquivalentTo (demo.build()));
    }
}

TEST_CASE ("the committed demo files are byte for byte what the factory writes", "[demos]")
{
    // The case above compares TREES, and ValueTree::isEquivalentTo does not
    // compare property order - NamedValueSet falls back to a key lookup the
    // moment the orders differ. So it passes happily through a schema reshuffle
    // that leaves every file under examples/ stale, and the drift is invisible
    // until someone diffs a file they just saved.
    //
    // Two comments in ProjectSchema.cpp claim the examples are byte-compared
    // against the factory. This is the test that makes that true.
    //
    // Regenerate with `dew_render --write-demos examples` when it fails.
    const juce::File examples (DEW_EXAMPLES_DIR);

    for (const auto& demo : ProjectFactory::demos())
    {
        const auto file = examples.getChildFile (demo.fileName);
        INFO ("file: " << file.getFullPathName());

        REQUIRE (file.existsAsFile());

        // Normalised the way writeToFile normalises it. juce::JSON::toString
        // ends its lines with CRLF and writeToFile rewrites them to LF, so the
        // two halves of this comparison are the same bytes only once that has
        // been done to both - and trim() would not do it, because the endings
        // that differ are in the middle.
        REQUIRE (ProjectSerializer::toJsonString (demo.build()).replace ("\r\n", "\n").trim()
                 == file.loadFileAsString().replace ("\r\n", "\n").trim());
    }
}

TEST_CASE ("what a demo is built from does not depend on a locale", "[demos][i18n]")
{
    // The committed demos are compared byte for byte against what the factory
    // builds, and the factory's channel, pattern, track and insert names now
    // come from the catalogue. So the artefact would move the day a second
    // language shipped - unless what feeds it is pinned, which is what
    // ProjectFactory::createDefault's REFERENCE default does.
    //
    // Pinned by construction rather than by ambient state: there is no overload
    // that reads the active locale, so a demo builder that wanted the reader's
    // language would have to ask for it in writing. Setting a locale here and
    // finding the tree unchanged is what says so.
    const auto reference = ProjectFactory::createDefault();

    const auto previous = activeLocale();
    setLocale ("de-CH");
    const auto underAnotherLocale = ProjectFactory::createDefault();
    setLocale (previous);

    CHECK (underAnotherLocale.isEquivalentTo (reference));

    // And File > New is the one that does follow the reader. Equal here today
    // because English is the only catalogue this build carries; what the check
    // holds is that the call site asked, which is the part that has to survive
    // a second one.
    CHECK (ProjectFactory::createDefault (activeLocale()).isEquivalentTo (reference));

    // Control case: the names really do come from the catalogue, so the two
    // comparisons above are comparing something.
    const auto channel = reference.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());
    CHECK (channel[ids::name].toString() == tr (StringId::project_kick));
}

TEST_CASE ("no demo ships two patterns with the same id", "[demos]")
{
    // Nothing rejects a duplicate: ProjectEdits::findPattern returns the first
    // match, so a second pattern with id 1 is simply unreachable and every clip
    // pointing at it plays the other one. Three demos had exactly that, because
    // createDefault() already ships a "Pattern 1" and they appended their own.
    juce::StringArray warnings;

    for (int i = 0; i < (int) ProjectFactory::demos().size(); ++i)
    {
        const auto project = DemoLibrary::load (i, warnings);
        INFO ("demo: " << keyOf (ProjectFactory::demos()[(size_t) i].menuName));

        juce::Array<int> seen;

        for (const auto& pattern : project)
        {
            if (! pattern.hasType (ids::PATTERN))
                continue;

            const auto id = (int) pattern[ids::id];
            INFO ("pattern id " << id);
            REQUIRE (! seen.contains (id));
            seen.add (id);
        }
    }
}

TEST_CASE ("every demo clip points at something that exists", "[demos]")
{
    juce::StringArray warnings;

    for (int i = 0; i < (int) ProjectFactory::demos().size(); ++i)
    {
        const auto project = DemoLibrary::load (i, warnings);
        INFO ("demo: " << keyOf (ProjectFactory::demos()[(size_t) i].menuName));

        juce::Array<int> patterns, automations;

        for (const auto& child : project)
        {
            if (child.hasType (ids::PATTERN))
                patterns.add ((int) child[ids::id]);

            if (child.hasType (ids::AUTOMATION))
                automations.add ((int) child[ids::id]);
        }

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            for (const auto& clip : track)
            {
                if (! clip.hasType (ids::CLIP))
                    continue;

                const auto kind = clip[ids::kind].toString();

                if (kind == "pattern")
                {
                    INFO ("clip -> pattern " << (int) clip[ids::patternId]);
                    REQUIRE (patterns.contains ((int) clip[ids::patternId]));
                }
                else if (kind == "automation")
                {
                    INFO ("clip -> automation " << (int) clip[ids::automationId]);
                    REQUIRE (automations.contains ((int) clip[ids::automationId]));
                }
            }
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

        int masterEffects = 0;

        for (const auto& effect :
             project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER))
            if (effect.hasType (ids::EFFECT))
            {
                ++masterEffects;
                types.addIfNotAlreadyThere (effect[ids::type].toString());
            }

        REQUIRE (channelEffects > 0);
        REQUIRE (trackEffects > 0);
        REQUIRE (types.size() >= 5);

        // The third place a chain can live. Implemented end to end since the
        // master became a bus like any other, and empty in every shipped file
        // until this demo used it.
        REQUIRE (masterEffects > 0);

        // The rest of what the chain editor can do, none of which any shipped
        // project had ever contained: a chain at the documented maximum depth,
        // a slot switched off rather than removed, and a filter that is not a
        // lowpass.
        int deepest = 0;
        bool bypassed = false;
        juce::StringArray filterModes;

        for (const auto& channel : project)
        {
            if (! channel.hasType (ids::CHANNEL))
                continue;

            deepest = juce::jmax (deepest, countIn (channel, ids::EFFECT));

            for (const auto& effect : channel)
            {
                if (! effect.hasType (ids::EFFECT))
                    continue;

                if (! (bool) effect[ids::enabled])
                    bypassed = true;

                if (effect[ids::type].toString() == "filter")
                    filterModes.addIfNotAlreadyThere (effect[ids::filterMode].toString());
            }
        }

        REQUIRE (deepest == kMaxEffectsPerChain);
        REQUIRE (bypassed);
        REQUIRE (filterModes.size() >= 3);
    }

    SECTION ("the getting-started demo is an arrangement, not a loop")
    {
        const auto project = DemoLibrary::load (indexOfFile ("demo.dew"), warnings);

        int clips = 0;

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            clips += countIn (track, ids::CLIP);

        REQUIRE (countIn (project, ids::PATTERN) >= 3);
        REQUIRE (clips >= 4);
    }

    SECTION ("the wavetable demo uses the bank, unison and a position curve")
    {
        const auto project = DemoLibrary::load (indexOfFile ("wavetable.dew"), warnings);

        juce::StringArray tables, sources;
        int widest = 1, wavetableSlots = 0;

        for (const auto& channel : project)
        {
            if (! channel.hasType (ids::CHANNEL))
                continue;

            for (const auto& osc : channel.getChildWithName (ids::INSTRUMENT))
            {
                if (! osc.hasType (ids::OSC) || ! (bool) osc[ids::enabled])
                    continue;

                if (osc[ids::mode].toString() != "wavetable")
                    continue;

                ++wavetableSlots;
                tables.addIfNotAlreadyThere (osc[ids::wavetable].toString());
                sources.addIfNotAlreadyThere (osc[ids::wavePositionSource].toString());
                widest = juce::jmax (widest, (int) osc[ids::unisonVoices]);
            }
        }

        INFO ("tables: " << tables.joinIntoString (", "));
        REQUIRE (wavetableSlots >= 5);
        REQUIRE (tables.size() >= 4);

        // Both ways a position can move. A demo with only one of them shows
        // half of what the control does.
        REQUIRE (sources.size() == 2);
        REQUIRE (widest > 1);

        // And the position is DRAWN, not just set: channelOsc is the scope this
        // demo exists to put in front of somebody.
        bool morphed = false;

        for (const auto& automation : project)
            if (automation.hasType (ids::AUTOMATION)
                && automation[ids::scope].toString() == "channelOsc")
                morphed = true;

        REQUIRE (morphed);
    }

    SECTION ("the oscillator-stack demo stacks oscillators")
    {
        const auto project = DemoLibrary::load (indexOfFile ("layers.dew"), warnings);

        int fullStacks = 0;
        bool detuned = false, mixedModes = false;

        for (const auto& channel : project)
        {
            if (! channel.hasType (ids::CHANNEL))
                continue;

            juce::Array<int> octaves;
            juce::StringArray modes;
            int enabled = 0;

            for (const auto& osc : channel.getChildWithName (ids::INSTRUMENT))
            {
                if (! osc.hasType (ids::OSC) || ! (bool) osc[ids::enabled])
                    continue;

                ++enabled;
                octaves.addIfNotAlreadyThere ((int) osc[ids::octave]);
                modes.addIfNotAlreadyThere (osc[ids::mode].toString());

                if ((int) osc[ids::detuneCents] != 0)
                    detuned = true;
            }

            if (enabled == kMaxOscillators && octaves.size() > 1)
                ++fullStacks;

            // A slot's mode is the SLOT's, not the channel's - so a wavetable
            // can stand beside two classic oscillators.
            if (enabled > 1 && modes.size() > 1)
                mixedModes = true;
        }

        REQUIRE (fullStacks >= 3);
        REQUIRE (detuned);
        REQUIRE (mixedModes);
    }

    SECTION ("the song-structure demo arranges across lanes, and shares a bus")
    {
        const auto project = DemoLibrary::load (indexOfFile ("arrangement.dew"), warnings);

        REQUIRE ((int) project[ids::barsInSong] >= 24);
        REQUIRE (countIn (project, ids::PATTERN) >= 5);

        int lanesWithClips = 0;

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            if (countIn (track, ids::CLIP) > 0)
                ++lanesWithClips;

        REQUIRE (lanesWithClips >= 3);

        // More channels than inserts, which only happens when several of them
        // share one - the bus routing nothing else in the library shows.
        juce::Array<int> buses;
        int channels = 0;

        for (const auto& channel : project)
            if (channel.hasType (ids::CHANNEL))
            {
                ++channels;
                buses.addIfNotAlreadyThere ((int) channel[ids::mixerTrackId]);
            }

        INFO (channels << " channels over " << buses.size() << " inserts");
        REQUIRE (channels > buses.size());
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

        // Not four copies of the same curve pointed at the same kind of thing.
        // Every scope but channelOsc, which is the wavetable demo's, and a
        // parameter that is not `cutoff` - the only one ever automated before.
        juce::StringArray scopes, params;
        bool bent = false, stepped = false;

        for (const auto& automation : project)
        {
            if (! automation.hasType (ids::AUTOMATION))
                continue;

            scopes.addIfNotAlreadyThere (automation[ids::scope].toString());
            params.addIfNotAlreadyThere (automation[ids::param].toString());

            for (const auto& point : automation)
            {
                if (! point.hasType (ids::POINT))
                    continue;

                if (! juce::exactlyEqual ((double) point[ids::curve], 0.0))
                    bent = true;

                if (point[ids::shape].toString() == "step")
                    stepped = true;
            }
        }

        INFO ("scopes: " << scopes.joinIntoString (", "));
        INFO ("params: " << params.joinIntoString (", "));
        REQUIRE (scopes.size() >= 4);
        REQUIRE (params.size() >= 4);

        // A segment has a SHAPE and a BEND, and every point in every file the
        // library shipped was a straight line at zero.
        REQUIRE (bent);
        REQUIRE (stepped);

        // And the engine agrees they will do something.
        const auto snapshot = buildSnapshot (project, nullptr);
        REQUIRE (snapshot.anyAutomation);
        REQUIRE (snapshot.anyEffects);
    }
}
