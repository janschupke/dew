#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "engine/EngineSnapshot.h"
#include "i18n/Strings.h"
#include "io/OfflineRenderer.h"
#include "model/DemoLibrary.h"
#include "model/EffectType.h"
#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/Meter.h"
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

TEST_CASE ("every demo's arrangement reaches the end of its song", "[demos]")
{
    // barsInSong is the document's extent and the clips are its content, so a
    // demo whose content stops a third of the way in has one of the two wrong.
    //
    // This is the assertion whose absence let a real regression ship: when
    // clips became steps, ScoreBake went on handing addClip bar numbers, and
    // the three score-baked demos were committed with their arrangements
    // crushed into a sixteenth of the timeline. Every other demo test passed -
    // the clips still pointed at patterns that existed, and four steps of a
    // pattern are audible.
    juce::StringArray warnings;

    for (int i = 0; i < (int) ProjectFactory::demos().size(); ++i)
    {
        const auto project = DemoLibrary::load (i, warnings);
        INFO ("demo: " << keyOf (ProjectFactory::demos()[(size_t) i].menuName));

        const auto stepsPerBar = Meter::of (project).stepsPerBar();
        const auto bars = (int) project[ids::barsInSong];
        auto furthest = 0;

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            for (const auto& clip : track)
                if (clip.hasType (ids::CLIP))
                    furthest = juce::jmax (furthest, (int) clip[ids::startStep]
                                                         + (int) clip[ids::lengthSteps]);

        INFO ("song " << bars << " bars of " << stepsPerBar << " steps; content reaches step "
                      << furthest);

        // Into the last bar, not exactly onto its end: trailing silence inside
        // the final bar is a musical choice, and a whole empty bar is not.
        REQUIRE (furthest > (bars - 1) * stepsPerBar);
    }
}

TEST_CASE ("the library as a whole exercises the app", "[demos]")
{
    // This replaced a case per demo, each asserting that the demo named after a
    // feature used that feature. That framing was the reason large parts of the
    // app were demonstrated by nothing at all: it proved the wavetable demo used
    // wavetables and had no opinion about whether ANYTHING used the FM matrix,
    // the LFO, four of the ten effect types, three automation scopes or any
    // metre but 4/4 - and none of them did.
    //
    // The question worth asking is about the LIBRARY. It fails when a capability
    // stops being demonstrated, which is the property the library exists to
    // have, and it does not have to be edited when a track is rewritten.
    juce::StringArray warnings;

    juce::StringArray effectTypes, filterModes, distortionModes, waveforms, wavetables,
        positionSources, scopes, pointShapes, instrumentTypes, chainHosts;
    juce::Array<int> stepsPerBeat;
    juce::StringArray meters;

    auto fmRouted = false, lfoOn = false, bentCurve = false, pannedChannel = false,
         mutedLane = false, partialMix = false, bypassedEffect = false, sharedInsert = false,
         subBarClip = false, laneGain = false;

    const auto& demos = ProjectFactory::demos();
    REQUIRE (demos.size() == 10);

    for (int i = 0; i < (int) demos.size(); ++i)
    {
        auto project = DemoLibrary::load (i, warnings);
        INFO ("demo: " << keyOf (demos[(size_t) i].menuName));

        const auto meter = Meter::of (project);
        meters.addIfNotAlreadyThere (meter.toString());
        stepsPerBeat.addIfNotAlreadyThere (meter.stepsPerBeat);

        // Every track is a track, not a loop and not an album side.
        const auto bars = (int) project[ids::barsInSong];
        INFO ("bars: " << bars);
        REQUIRE (bars >= 32);
        REQUIRE (bars <= 64);

        juce::Array<int> insertsUsed;

        const auto readChain = [&] (const juce::ValueTree& host, const char* where)
        {
            for (const auto& effect : host)
            {
                if (! effect.hasType (ids::EFFECT))
                    continue;

                chainHosts.addIfNotAlreadyThere (where);
                effectTypes.addIfNotAlreadyThere (effect[ids::type].toString());

                if (! (bool) effect[ids::enabled])
                    bypassedEffect = true;

                if ((double) effect[ids::mix] < 1.0)
                    partialMix = true;

                if (effect[ids::type].toString() == "filter")
                    filterModes.addIfNotAlreadyThere (effect[ids::filterMode].toString());

                if (effect[ids::type].toString() == "distortion")
                    distortionModes.addIfNotAlreadyThere (effect[ids::distortionMode].toString());
            }
        };

        for (const auto& channel : project)
        {
            if (! channel.hasType (ids::CHANNEL))
                continue;

            instrumentTypes.addIfNotAlreadyThere (channel[ids::source].toString());
            readChain (channel, "channel");

            const auto insert = (int) channel[ids::mixerTrackId];

            if (insertsUsed.contains (insert))
                sharedInsert = true;

            insertsUsed.add (insert);

            if (std::abs ((double) channel[ids::pan]) > 0.001)
                pannedChannel = true;

            for (const auto& osc : channel.getChildWithName (ids::INSTRUMENT))
            {
                if (! osc.hasType (ids::OSC))
                    continue;

                // The matrix reads as routed when it is not the identity: any
                // amount above zero, or an output that is not fully open. The
                // shipped library had every cell at its default, which is three
                // oscillators summed in parallel.
                for (const auto& to : { ids::fmTo1, ids::fmTo2, ids::fmTo3 })
                    if ((double) osc[to] > 0.001)
                        fmRouted = true;

                if ((double) osc[ids::fmOut] < 0.999)
                    fmRouted = true;

                if (const auto lfo = osc.getChildWithName (ids::LFO);
                    lfo.isValid() && (bool) lfo[ids::lfoOn])
                    lfoOn = true;

                if (! (bool) osc[ids::enabled])
                    continue;

                if (osc[ids::mode].toString() == "wavetable")
                {
                    const auto node = osc.getChildWithName (ids::WAVETABLE);
                    wavetables.addIfNotAlreadyThere (node[ids::wavetable].toString());
                    positionSources.addIfNotAlreadyThere (node[ids::wavePositionSource].toString());
                }
                else
                {
                    waveforms.addIfNotAlreadyThere (
                        osc.getChildWithName (ids::CLASSIC)[ids::wave].toString());
                }
            }
        }

        const auto mixer = project.getChildWithName (ids::MIXER);

        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
                readChain (track, "mixerTrack");

        readChain (mixer.getChildWithName (ids::MASTER), "master");

        for (const auto& automation : project)
        {
            if (! automation.hasType (ids::AUTOMATION))
                continue;

            scopes.addIfNotAlreadyThere (automation[ids::scope].toString());

            for (const auto& point : automation)
            {
                if (! point.hasType (ids::POINT))
                    continue;

                pointShapes.addIfNotAlreadyThere (point[ids::shape].toString());

                if (std::abs ((double) point[ids::curve]) > 0.001)
                    bentCurve = true;
            }
        }

        const auto bar = meter.stepsPerBar();

        for (const auto& lane : project.getChildWithName (ids::PLAYLIST))
        {
            if (! lane.hasType (ids::PLAYLIST_TRACK))
                continue;

            if ((bool) lane[ids::mute])
                mutedLane = true;

            if (std::abs ((double) lane[ids::gain] - 1.0) > 0.001)
                laneGain = true;

            for (const auto& clip : lane)
                if (clip.hasType (ids::CLIP) && ((int) clip[ids::startStep]) % bar != 0)
                    subBarClip = true;
        }
    }

    INFO ("effects: " << effectTypes.joinIntoString (" "));
    INFO ("scopes: " << scopes.joinIntoString (" "));
    INFO ("wavetables: " << wavetables.joinIntoString (" "));
    INFO ("metres: " << meters.joinIntoString (" "));

    SECTION ("every effect type, filter mode and distortion mode is heard somewhere")
    {
        CHECK (effectTypes.size() == kNumEffectTypes);
        CHECK (filterModes.size() == 3);
        CHECK (distortionModes.size() == 4);
        CHECK (chainHosts.size() == 3);
        CHECK (bypassedEffect);
        CHECK (partialMix);
    }

    SECTION ("every generator the synth has is played by something")
    {
        CHECK (waveforms.size() == 4);
        CHECK (wavetables.size() == 5);
        CHECK (positionSources.size() == 2);

        // The two that were dead in every shipped file.
        CHECK (fmRouted);
        CHECK (lfoOn);
    }

    SECTION ("every automation scope but the one that needs a soundfont is drawn")
    {
        // channelSoundFont is the exception, and it is an honest one: reaching
        // it needs an .sf2 committed to this repository, which is a licensing
        // and repository-size decision rather than a musical one. When a font
        // ships, this list grows by one and so does the library.
        for (const auto* scope : { "project", "channel", "channelOsc", "channelAmp",
                                   "channelEffect", "mixerTrack", "mixerEffect", "master" })
        {
            INFO ("scope: " << scope);
            CHECK (scopes.contains (scope));
        }

        CHECK (! scopes.contains ("channelSoundFont"));

        // Both stored shapes, and a bend that is not zero. Before this library
        // every curve in every demo was a straight line.
        CHECK (pointShapes.contains ("curve"));
        CHECK (pointShapes.contains ("step"));
        CHECK (bentCurve);
    }

    SECTION ("the mixer and the playlist are used for what they are for")
    {
        CHECK (sharedInsert); // several channels on one fader: a drum bus
        CHECK (pannedChannel);
        CHECK (mutedLane);
        CHECK (laneGain);
        CHECK (subBarClip); // a clip off the bar line, which v20 made sayable
    }

    SECTION ("the library is not all in one metre at one grid")
    {
        // The whole shipped library was 4/4 at four steps to a beat, so Meter
        // and the grid arithmetic were demonstrated by nothing.
        CHECK (meters.size() >= 4);
        CHECK (stepsPerBeat.size() >= 3);
    }

    SECTION ("what is NOT covered is covered deliberately")
    {
        // Only the synth. `audio` needs a recording committed beside the demos
        // and resolved from the binary the Demos menu loads from; `soundfont`
        // needs a third-party .sf2. Both are asserted so that adding one is a
        // deliberate edit here rather than a silent change of scope.
        CHECK (instrumentTypes.size() == 1);
        CHECK (instrumentTypes.contains ("synth"));
    }
}
