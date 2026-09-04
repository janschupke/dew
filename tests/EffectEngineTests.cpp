// Effects in a real render, in the document, and in the module pool.
//
// Split out of an EffectTests.cpp that was 1,746 lines, along the Catch2
// tags it already carried.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/AudioEngine.h"
#include "engine/EngineSnapshot.h"
#include "io/OfflineRenderer.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

#include "EffectDspHarness.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("an effect on a channel is heard in a real render", "[effects][render]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    RenderOptions options;
    options.seconds = 2.0;
    options.mode = Transport::Mode::pattern;

    juce::AudioBuffer<float> plain, filtered;
    const auto plainReport = OfflineRenderer::renderToBuffer (project, plain, options);
    REQUIRE (plainReport.ok());
    REQUIRE (plainReport.rms > 0.0f);

    // A steep lowpass on every channel must take the whole mix down.
    for (auto owner : ProjectEdits::effectChainOwners (project))
    {
        if (! owner.hasType (ids::CHANNEL))
            continue;

        auto effect = ProjectEdits::addEffect (project, owner, "filter", &undo);
        REQUIRE (effect.isValid());
        effect.setProperty (ids::cutoff, 90.0, &undo);
        effect.setProperty (ids::resonance, 0.4, &undo);
    }

    const auto filteredReport = OfflineRenderer::renderToBuffer (project, filtered, options);
    REQUIRE (filteredReport.ok());
    REQUIRE (filteredReport.warnings.isEmpty());

    INFO ("plain rms " << plainReport.rms << ", filtered rms " << filteredReport.rms);
    REQUIRE (filteredReport.rms < plainReport.rms * 0.6f);
    REQUIRE (filteredReport.rms > 0.0f);
}

TEST_CASE ("an effect on a mixer track is heard in a real render", "[effects][render]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    RenderOptions options;
    options.seconds = 2.0;
    options.mode = Transport::Mode::pattern;

    juce::AudioBuffer<float> plain, driven;
    const auto plainReport = OfflineRenderer::renderToBuffer (project, plain, options);

    for (auto owner : ProjectEdits::effectChainOwners (project))
    {
        if (! owner.hasType (ids::MIXER_TRACK))
            continue;

        auto effect = ProjectEdits::addEffect (project, owner, "drive", &undo);
        REQUIRE (effect.isValid());
        effect.setProperty (ids::drive, 30.0, &undo);
    }

    const auto drivenReport = OfflineRenderer::renderToBuffer (project, driven, options);
    REQUIRE (drivenReport.ok());

    // Saturation raises the average level relative to the peak.
    const auto crest = [] (const RenderReport& r) { return r.rms > 0.0f ? r.peak / r.rms : 0.0f; };

    INFO ("crest plain " << crest (plainReport) << " driven " << crest (drivenReport));
    REQUIRE (crest (drivenReport) < crest (plainReport));
}

TEST_CASE ("a chain survives save and load, and renders identically", "[effects][render][schema]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    auto filter = ProjectEdits::addEffect (project, channel, "filter", &undo);
    filter.setProperty (ids::cutoff, 700.0, &undo);
    auto delay = ProjectEdits::addEffect (project, channel, "delay", &undo);
    delay.setProperty (ids::delayMs, 180.0, &undo);
    delay.setProperty (ids::mix, 0.4, &undo);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.result.wasOk());
    REQUIRE (loaded.warnings.isEmpty());

    RenderOptions options;
    options.seconds = 1.5;
    options.mode = Transport::Mode::pattern;

    juce::AudioBuffer<float> before, after;
    OfflineRenderer::renderToBuffer (project, before, options);
    OfflineRenderer::renderToBuffer (loaded.tree, after, options);

    REQUIRE (before.getNumSamples() == after.getNumSamples());

    for (int i = 0; i < before.getNumSamples(); i += 97)
        REQUIRE_THAT (after.getSample (0, i), WithinAbs (before.getSample (0, i), 1.0e-6));
}

TEST_CASE ("effects keep their DSP state when unrelated things change", "[effects][snapshot]")
{
    // The pool is keyed on effect id rather than position. Keyed on position,
    // adding an effect anywhere EARLIER in build order would shift every later
    // effect onto a different unit and cut whatever tail it was in the middle
    // of - so the perturbation below has to be earlier, or the test proves
    // nothing. Build order is mixer tracks, then channels.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    juce::Array<juce::ValueTree> mixerTracks;

    for (const auto& track : project.getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK))
            mixerTracks.add (track);

    REQUIRE (mixerTracks.size() > 2);

    auto reverb = ProjectEdits::addEffect (project, mixerTracks.getLast(), "reverb", &undo);
    REQUIRE (reverb.isValid());
    const auto reverbId = (int) reverb[ids::id];

    // Find the pool unit an effect id ended up on, wherever in the project it
    // lives. Searching by id is the point: a lookup by position would pass even
    // if the assignment were positional.
    const auto unitFor = [] (const EngineSnapshot& snapshot, int id)
    {
        const auto findIn = [id] (const EffectChainSnapshot& chain)
        {
            for (int i = 0; i < chain.numSlots; ++i)
                if (chain.slots[(size_t) i].id == id)
                    return chain.slots[(size_t) i].unitIndex;

            return -1;
        };

        for (const auto& channel : snapshot.channels)
            if (const auto unit = findIn (channel.effects); unit >= 0)
                return unit;

        for (const auto& track : snapshot.mixerTracks)
            if (const auto unit = findIn (track.effects); unit >= 0)
                return unit;

        return -1;
    };

    const auto unitBefore = unitFor (buildSnapshot (project, nullptr), reverbId);
    REQUIRE (unitBefore >= 0);

    // Two effects on the FIRST mixer track, well ahead of the reverb.
    auto added = ProjectEdits::addEffect (project, mixerTracks.getFirst(), "filter", &undo);
    ProjectEdits::addEffect (project, mixerTracks.getFirst(), "drive", &undo);

    REQUIRE (unitFor (buildSnapshot (project, nullptr), reverbId) == unitBefore);

    // And removing one from ahead of it does not move it back either.
    ProjectEdits::removeEffect (mixerTracks.getFirst(), added, &undo);
    REQUIRE (unitFor (buildSnapshot (project, nullptr), reverbId) == unitBefore);

    // Rebuilding an unchanged document is deterministic, which is what makes
    // any of this stable in the first place.
    REQUIRE (unitFor (buildSnapshot (project, nullptr), reverbId) == unitBefore);
}

TEST_CASE ("a chain refuses to grow past what the engine renders", "[effects][edits]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);

    for (int i = 0; i < kMaxEffectsPerChain; ++i)
        REQUIRE (ProjectEdits::addEffect (project, channel, "filter", &undo).isValid());

    // The next one would be saved but never heard, which reads as a broken
    // effect rather than as a full chain.
    REQUIRE (! ProjectEdits::addEffect (project, channel, "reverb", &undo).isValid());
    REQUIRE (ProjectEdits::countEffects (channel) == kMaxEffectsPerChain);
}

TEST_CASE ("effect ids are unique across the whole project", "[effects][edits]")
{
    // The engine keys DSP state on the effect id, so two effects sharing one
    // would fight over the same reverb tank.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    juce::Array<int> ids;

    for (auto owner : ProjectEdits::effectChainOwners (project))
    {
        auto effect = ProjectEdits::addEffect (project, owner, "filter", &undo);

        if (effect.isValid())
        {
            const auto id = (int) effect[ids::id];
            REQUIRE (! ids.contains (id));
            ids.add (id);
        }
    }

    REQUIRE (ids.size() > 4);
}

TEST_CASE ("the master chain is one of the owners a new id is derived from", "[effects][edits]")
{
    // The master carries a chain like any other bus. While it was left out of
    // effectChainOwners, the scan that picks `highest + 1` could not see what
    // was already there, so a second master effect took the id the first one
    // had - and the pool, which keys on the id, handed both the same module.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto master = project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());

    const auto first = ProjectEdits::addEffect (project, master, "eq", &undo);
    const auto second = ProjectEdits::addEffect (project, master, "reverb", &undo);

    REQUIRE (first.isValid());
    REQUIRE (second.isValid());
    REQUIRE ((int) first[ids::id] != (int) second[ids::id]);

    // And an effect added elsewhere afterwards clears both of them, rather than
    // colliding with a master effect the scan still could not see.
    const auto elsewhere = ProjectEdits::addEffect (
        project, project.getChildWithName (ids::CHANNEL), "delay", &undo);
    REQUIRE (elsewhere.isValid());
    REQUIRE ((int) elsewhere[ids::id] != (int) first[ids::id]);
    REQUIRE ((int) elsewhere[ids::id] != (int) second[ids::id]);
}

TEST_CASE ("reordering a chain does not disturb the instrument beside it", "[effects][edits]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);

    auto a = ProjectEdits::addEffect (project, channel, "filter", &undo);
    auto b = ProjectEdits::addEffect (project, channel, "delay", &undo);
    auto c = ProjectEdits::addEffect (project, channel, "reverb", &undo);

    const auto typesInOrder = [&channel]
    {
        juce::StringArray types;

        for (const auto& child : channel)
            if (child.hasType (ids::EFFECT))
                types.add (child[ids::type].toString());

        return types;
    };

    REQUIRE (typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });

    ProjectEdits::moveEffect (channel, c, 0, &undo);
    REQUIRE (typesInOrder() == juce::StringArray { "reverb", "filter", "delay" });

    ProjectEdits::moveEffect (channel, a, 2, &undo);
    REQUIRE (typesInOrder() == juce::StringArray { "reverb", "delay", "filter" });

    // The instrument is still there and still a child of the channel.
    REQUIRE (channel.getChildWithName (ids::INSTRUMENT).isValid());
    juce::ignoreUnused (b);
}

TEST_CASE ("a chain whose slots are all disabled renders as if it had none", "[effects][render]")
{
    // Switching an effect off must change nothing at all about the render -
    // not "almost nothing", which is what a tone change sounds like when you
    // are trying to A/B a chain.
    //
    // What this test is NOT: a detector for which code path ran. The engine
    // used to take the stereo detour whenever a chain had slots, enabled or
    // not - clear a scratch pair, pan into it, run a chain that does nothing,
    // add it back - and now takes the cheap mono path unless a slot is actually
    // on. This passes either way, deliberately, because the two paths ARE the
    // same arithmetic: adding into a cleared buffer and then into the track
    // equals adding into the track. That equivalence is what licenses the fast
    // path, so the equivalence is what is pinned here. If it ever stops holding,
    // the optimisation stops being one.
    // The demo, not createDefault(): a project with no notes renders to nothing
    // and the renderer refuses it outright, which would make this pass on two
    // failures rather than on two identical renders.
    auto withNone = dew::testing::fixtureProject();

    auto withDisabled = withNone.createCopy();
    auto channel = withDisabled.getChild (0);
    REQUIRE (channel.hasType (ids::CHANNEL));

    auto added = ProjectEdits::addEffect (withDisabled, channel, "reverb", nullptr);
    REQUIRE (added.isValid());
    added.setProperty (ids::enabled, false, nullptr);

    auto second = ProjectEdits::addEffect (withDisabled, channel, "delay", nullptr);
    REQUIRE (second.isValid());
    second.setProperty (ids::enabled, false, nullptr);

    juce::AudioBuffer<float> a, b;
    REQUIRE (OfflineRenderer::renderToBuffer (withNone, a).ok());
    REQUIRE (OfflineRenderer::renderToBuffer (withDisabled, b).ok());

    REQUIRE (a.getNumSamples() == b.getNumSamples());
    REQUIRE (a.getNumSamples() > 0);
    REQUIRE (a.getMagnitude (0, a.getNumSamples()) > 0.05f);

    for (int channelIndex = 0; channelIndex < a.getNumChannels(); ++channelIndex)
    {
        const auto* left = a.getReadPointer (channelIndex);
        const auto* right = b.getReadPointer (channelIndex);

        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            if (! juce::exactlyEqual (left[i], right[i]))
            {
                INFO ("channel " << channelIndex << ", sample " << i);
                REQUIRE (juce::exactlyEqual (left[i], right[i]));
            }
        }
    }
}

TEST_CASE ("effect DSP is built only for what a project uses", "[effects][pool]")
{
    // Every pool unit used to hold every effect type at once - a filter, a
    // reverb tank, a one-second delay line, a chorus and six biquads - all
    // constructed and prepared up front. About 0.45MB per unit, thirty-two
    // units, so roughly 15MB per AudioEngine at 44.1kHz and 30MB at 96k,
    // whether the project had one effect or none. The offline renderer builds a
    // fresh engine for every render and paid it every time.
    AudioEngine engine;
    engine.prepare (kDefaultSampleRate, 512);

    REQUIRE (engine.getMaterialisedEffectModuleCount() == 0);

    juce::StringArray warnings;
    engine.setProject (ProjectFactory::createDefault(), &warnings);
    REQUIRE (engine.getMaterialisedEffectModuleCount() == 0);

    auto project = ProjectFactory::createDefault();
    auto channel = project.getChild (0);
    REQUIRE (channel.hasType (ids::CHANNEL));

    ProjectEdits::addEffect (project, channel, "reverb", nullptr);
    ProjectEdits::addEffect (project, channel, "delay", nullptr);

    engine.setProject (project, &warnings);
    REQUIRE (engine.getMaterialisedEffectModuleCount() == 2);

    // Publishing the same project again reuses what is there rather than
    // building it twice - which is also what keeps a pointer in an older
    // snapshot valid.
    engine.setProject (project, &warnings);
    REQUIRE (engine.getMaterialisedEffectModuleCount() == 2);
}

TEST_CASE ("a project with more effects than the old pool held keeps all of them",
           "[effects][pool]")
{
    // The pool was capped at 32 units while the schema permits four effects on
    // each of 64 channels and 32 mixer tracks plus the master. Past 32,
    // buildSnapshot warned and then stopped reading that chain - and the
    // warning went to an argument whose default was nullptr.
    auto project = dew::testing::fixtureProject();

    auto added = 0;

    for (auto channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        while (ProjectEdits::countEffects (channel) < kMaxEffectsPerChain)
        {
            ProjectEdits::addEffect (project, channel, "drive", nullptr);
            ++added;
        }
    }

    REQUIRE (added > 0);

    juce::StringArray warnings;
    juce::AudioBuffer<float> rendered;

    RenderOptions options;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered, options);

    INFO ("warnings: " << report.warnings.joinIntoString ("; "));
    REQUIRE (report.ok());
    REQUIRE (report.warnings.isEmpty());
}

TEST_CASE ("a slot switched away and back does not resume its old tail", "[effects][pool]")
{
    // Modules are keyed on (pool index, type) and are never destroyed, so
    // switching a slot from reverb to filter and back finds the reverb exactly
    // as it was left - with its tail still in the tanks. The chain runner
    // resets a unit whose type changed for this reason; without it, a slot
    // toggled between two effects would leak the first one's decay into the
    // second's output.
    auto module = createEffectModule (EffectType::reverb);
    REQUIRE (module != nullptr);
    module->prepare (sampleRate, blockSize);

    juce::AudioBuffer<float> dryScratch (2, blockSize);

    Params reverb { EffectType::reverb };
    reverb.set (ids::roomSize, 0.9f).set (ids::mix, 1.0f);

    // A tone for long enough to charge it, then silence: the tail is what is
    // left in the tanks. Long enough matters - juce::Reverb ramps its wet gain,
    // so a single block in produces almost nothing out and the test would be
    // measuring the ramp rather than the tail.
    auto burst = sineBuffer (440.0, blockSize * 16);

    for (int start = 0; start < burst.getNumSamples(); start += blockSize)
        processEffectSlot (
            *module, reverb.block, EffectType::reverb,
            { burst.getWritePointer (0) + start, burst.getWritePointer (1) + start, blockSize },
            dryScratch);

    juce::AudioBuffer<float> silence (2, blockSize);
    silence.clear();
    processEffectSlot (*module, reverb.block, EffectType::reverb,
                       { silence.getWritePointer (0), silence.getWritePointer (1), blockSize },
                       dryScratch);

    const auto tail = silence.getMagnitude (0, blockSize);
    INFO ("tail after the burst: " << tail);
    REQUIRE (tail > 1.0e-5f); // there IS something to leak

    // What the chain runner does when a slot's type changed.
    module->reset();

    juce::AudioBuffer<float> afterReset (2, blockSize);
    afterReset.clear();
    processEffectSlot (
        *module, reverb.block, EffectType::reverb,
        { afterReset.getWritePointer (0), afterReset.getWritePointer (1), blockSize }, dryScratch);

    REQUIRE (afterReset.getMagnitude (0, blockSize) < tail * 0.01f);
}
