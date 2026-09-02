#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Effects.h"
#include "engine/EngineSnapshot.h"
#include "engine/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

namespace
{

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;

/** Runs a signal through one effect and hands back the result. */
void runEffect (EffectType type, const EffectParams& params,
                juce::AudioBuffer<float>& buffer)
{
    EffectUnit unit;
    unit.prepare (sampleRate, blockSize);

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto n = juce::jmin (blockSize, buffer.getNumSamples() - start);
        unit.process (type, params,
                      buffer.getWritePointer (0) + start,
                      buffer.getWritePointer (1) + start, n);
    }
}

juce::AudioBuffer<float> sineBuffer (double frequency, int numSamples, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer (2, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto value = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi
                                                         * frequency * (double) i / sampleRate);
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }

    return buffer;
}

/** RMS of a window, which is how "did this get quieter" is actually measured. */
float rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    const auto n = juce::jmin (length, buffer.getNumSamples() - start);
    return n > 0 ? buffer.getRMSLevel (0, start, n) : 0.0f;
}

} // namespace

TEST_CASE ("a lowpass removes high content and keeps low content", "[effects][dsp]")
{
    EffectParams params;
    params.filterMode = FilterMode::lowpass;
    params.cutoff = 200.0f;
    params.resonance = 0.5f;

    // The same filter, on two tones an order of magnitude apart in frequency.
    auto low = sineBuffer (80.0, 22050);
    auto high = sineBuffer (5000.0, 22050);

    const auto lowBefore = rmsOf (low, 11025, 11025);
    const auto highBefore = rmsOf (high, 11025, 11025);

    runEffect (EffectType::filter, params, low);
    runEffect (EffectType::filter, params, high);

    const auto lowAfter = rmsOf (low, 11025, 11025);
    const auto highAfter = rmsOf (high, 11025, 11025);

    INFO ("80Hz " << lowBefore << " -> " << lowAfter
          << ", 5kHz " << highBefore << " -> " << highAfter);

    REQUIRE (lowAfter > lowBefore * 0.7f);          // barely touched
    REQUIRE (highAfter < highBefore * 0.05f);       // gone
}

TEST_CASE ("a highpass does the opposite", "[effects][dsp]")
{
    EffectParams params;
    params.filterMode = FilterMode::highpass;
    params.cutoff = 2000.0f;

    auto low = sineBuffer (80.0, 22050);
    auto high = sineBuffer (8000.0, 22050);

    const auto lowBefore = rmsOf (low, 11025, 11025);
    const auto highBefore = rmsOf (high, 11025, 11025);

    runEffect (EffectType::filter, params, low);
    runEffect (EffectType::filter, params, high);

    REQUIRE (rmsOf (low, 11025, 11025) < lowBefore * 0.05f);
    REQUIRE (rmsOf (high, 11025, 11025) > highBefore * 0.7f);
}

TEST_CASE ("a delay produces a repeat at the time it was set to", "[effects][dsp]")
{
    EffectParams params;
    params.delayMs = 200.0f;
    params.feedback = 0.0f;      // one repeat only, so the position is unambiguous
    params.mix = 1.0f;

    // A short click at the very start, then silence.
    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.0));
    buffer.clear();

    for (int i = 0; i < 64; ++i)
    {
        buffer.setSample (0, i, 0.8f);
        buffer.setSample (1, i, 0.8f);
    }

    runEffect (EffectType::delay, params, buffer);

    // Find where the energy actually landed.
    int loudest = -1;
    float loudestValue = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto value = std::abs (buffer.getSample (0, i));

        if (value > loudestValue)
        {
            loudestValue = value;
            loudest = i;
        }
    }

    const auto expected = (int) (0.200 * sampleRate);

    INFO ("peak at sample " << loudest << ", expected near " << expected);
    REQUIRE (loudestValue > 0.5f);
    REQUIRE (std::abs (loudest - expected) < 100);
}

TEST_CASE ("delay feedback makes repeats that decay rather than one or forever", "[effects][dsp]")
{
    EffectParams params;
    params.delayMs = 100.0f;
    params.feedback = 0.6f;

    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.0));
    buffer.clear();

    for (int i = 0; i < 64; ++i)
        for (int c = 0; c < 2; ++c)
            buffer.setSample (c, i, 0.8f);

    runEffect (EffectType::delay, params, buffer);

    const auto window = (int) (sampleRate * 0.05);
    const auto first  = rmsOf (buffer, (int) (sampleRate * 0.100) - window / 2, window);
    const auto second = rmsOf (buffer, (int) (sampleRate * 0.200) - window / 2, window);
    const auto third  = rmsOf (buffer, (int) (sampleRate * 0.300) - window / 2, window);

    INFO ("repeats: " << first << " " << second << " " << third);

    REQUIRE (first > 0.01f);
    REQUIRE (second > 0.001f);
    REQUIRE (second < first);
    REQUIRE (third < second);
}

TEST_CASE ("reverb extends a sound past where it ended", "[effects][dsp]")
{
    EffectParams params;
    params.roomSize = 0.85f;
    params.damping = 0.2f;
    params.mix = 1.0f;

    // A quarter second of tone, then silence.
    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.5));
    buffer.clear();

    auto tone = sineBuffer (440.0, (int) (sampleRate * 0.25));

    for (int c = 0; c < 2; ++c)
        buffer.copyFrom (c, 0, tone, c, 0, tone.getNumSamples());

    const auto tailBefore = rmsOf (buffer, (int) (sampleRate * 0.4), (int) (sampleRate * 0.2));
    REQUIRE (tailBefore < 1.0e-6f);      // silent before the reverb

    runEffect (EffectType::reverb, params, buffer);

    const auto tailAfter = rmsOf (buffer, (int) (sampleRate * 0.4), (int) (sampleRate * 0.2));
    const auto laterTail = rmsOf (buffer, (int) (sampleRate * 1.0), (int) (sampleRate * 0.2));

    INFO ("tail " << tailAfter << ", later " << laterTail);

    // There is now sound where there was none, and it is decaying.
    REQUIRE (tailAfter > 0.002f);
    REQUIRE (laterTail < tailAfter);
}

TEST_CASE ("drive adds harmonics rather than only volume", "[effects][dsp]")
{
    EffectParams params;
    params.drive = 20.0f;
    params.outputGain = 1.0f;

    auto buffer = sineBuffer (200.0, 8192, 0.5f);
    const auto before = buffer;

    runEffect (EffectType::drive, params, buffer);

    // A hard-driven sine becomes something square-ish: its peak sits far closer
    // to its RMS than a sine's does. Comparing crest factors says "the shape
    // changed" in a way that a gain change alone cannot fake.
    const auto crest = [] (const juce::AudioBuffer<float>& b)
    {
        const auto rms = b.getRMSLevel (0, 0, b.getNumSamples());
        return rms > 0.0f ? b.getMagnitude (0, 0, b.getNumSamples()) / rms : 0.0f;
    };

    INFO ("crest before " << crest (before) << " after " << crest (buffer));
    REQUIRE (crest (buffer) < crest (before) * 0.9f);
    REQUIRE (buffer.getMagnitude (0, 0, buffer.getNumSamples()) <= 1.05f);
}

TEST_CASE ("the EQ bands move the frequencies they name", "[effects][dsp]")
{
    const auto gainAt = [] (double frequency, const EffectParams& params)
    {
        auto buffer = sineBuffer (frequency, 22050);
        const auto before = rmsOf (buffer, 11025, 11025);
        runEffect (EffectType::eq, params, buffer);
        return rmsOf (buffer, 11025, 11025) / juce::jmax (1.0e-9f, before);
    };

    EffectParams flat;
    REQUIRE_THAT (gainAt (100.0, flat), WithinAbs (1.0, 0.05));
    REQUIRE_THAT (gainAt (900.0, flat), WithinAbs (1.0, 0.05));
    REQUIRE_THAT (gainAt (8000.0, flat), WithinAbs (1.0, 0.05));

    EffectParams boostLow;
    boostLow.lowGainDb = 12.0f;
    REQUIRE (gainAt (60.0, boostLow) > 2.5f);          // roughly +12dB
    REQUIRE_THAT (gainAt (8000.0, boostLow), WithinAbs (1.0, 0.1));

    EffectParams cutMid;
    cutMid.midGainDb = -18.0f;
    cutMid.midFreq = 900.0f;
    REQUIRE (gainAt (900.0, cutMid) < 0.3f);
    REQUIRE (gainAt (60.0, cutMid) > 0.9f);

    EffectParams boostHigh;
    boostHigh.highGainDb = 12.0f;
    REQUIRE (gainAt (12000.0, boostHigh) > 2.5f);
    REQUIRE_THAT (gainAt (60.0, boostHigh), WithinAbs (1.0, 0.1));
}

TEST_CASE ("chorus modulates rather than passing the signal through", "[effects][dsp]")
{
    EffectParams params;
    params.rate = 3.0f;
    params.depth = 0.8f;
    params.mix = 1.0f;

    auto buffer = sineBuffer (440.0, (int) (sampleRate * 1.0));
    const auto before = buffer;

    runEffect (EffectType::chorus, params, buffer);

    // Compare the second half, past the chorus's own delay: a modulated copy
    // differs from the original sample by sample even though its level is
    // similar. Identical output would mean the effect did nothing.
    double difference = 0.0;
    const auto start = buffer.getNumSamples() / 2;

    for (int i = start; i < buffer.getNumSamples(); ++i)
        difference += std::abs (buffer.getSample (0, i) - before.getSample (0, i));

    difference /= (double) (buffer.getNumSamples() - start);

    INFO ("mean difference " << difference);
    REQUIRE (difference > 0.02);
    REQUIRE (buffer.getRMSLevel (0, start, buffer.getNumSamples() - start) > 0.1f);
}

TEST_CASE ("a fully dry slot leaves the signal exactly as it was", "[effects][dsp]")
{
    EffectParams params;
    params.mix = 0.0f;
    params.cutoff = 100.0f;

    auto buffer = sineBuffer (5000.0, 4096);
    const auto before = buffer;

    runEffect (EffectType::filter, params, buffer);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (buffer.getSample (0, i), before.getSample (0, i)));
}

TEST_CASE ("mix blends between dry and wet", "[effects][dsp]")
{
    const auto highContentAt = [] (float mix)
    {
        EffectParams params;
        params.mix = mix;
        params.cutoff = 200.0f;

        auto buffer = sineBuffer (5000.0, 22050);
        runEffect (EffectType::filter, params, buffer);
        return rmsOf (buffer, 11025, 11025);
    };

    const auto dry = highContentAt (0.0f);
    const auto half = highContentAt (0.5f);
    const auto wet = highContentAt (1.0f);

    INFO ("dry " << dry << " half " << half << " wet " << wet);
    REQUIRE (half < dry * 0.7f);
    REQUIRE (half > wet);
}

// --- through the whole engine ------------------------------------------------

TEST_CASE ("an effect on a channel is heard in a real render", "[effects][render]")
{
    auto project = ProjectFactory::createDemo();
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
    auto project = ProjectFactory::createDemo();
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
    auto project = ProjectFactory::createDemo();
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
    auto project = ProjectFactory::createDemo();
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
    auto project = ProjectFactory::createDemo();
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

// --- the chain editor --------------------------------------------------------

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/EffectChainComponent.h"

namespace
{

struct ChainHarness
{
    ChainHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        chain.setSize (300, 400);
        chain.setVisible (true);
        chain.setOwner (channel());
    }

    juce::ValueTree channel() { return document.getState().getChildWithName (ids::CHANNEL); }

    juce::StringArray typesInOrder()
    {
        juce::StringArray types;

        for (const auto& child : channel())
            if (child.hasType (ids::EFFECT))
                types.add (child[ids::type].toString());

        return types;
    }

    ProjectDocument document;
    EditorState editorState;
    EffectChainComponent chain { document, editorState };
};

} // namespace

TEST_CASE ("the chain editor follows the chain it is pointed at", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    REQUIRE (h.chain.getNumSlotRows() == 0);

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == 1);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb" });

    h.chain.addEffectOfType ("delay");
    REQUIRE (h.chain.getNumSlotRows() == 2);

    // An edit made anywhere else still reaches the editor.
    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addEffect (h.document.getState(), h.channel(), "drive", &undo);
    REQUIRE (h.chain.getNumSlotRows() == 3);

    ProjectEdits::removeEffect (h.channel(), h.channel().getChildWithName (ids::EFFECT), &undo);
    REQUIRE (h.chain.getNumSlotRows() == 2);
}

TEST_CASE ("pointing the editor at another chain shows that chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();

    auto mixerTrack = h.document.getState().getChildWithName (ids::MIXER)
                                            .getChildWithName (ids::MIXER_TRACK);
    REQUIRE (mixerTrack.isValid());

    h.chain.addEffectOfType ("reverb");
    ProjectEdits::addEffect (h.document.getState(), mixerTrack, "eq", &undo);
    ProjectEdits::addEffect (h.document.getState(), mixerTrack, "drive", &undo);

    REQUIRE (h.chain.getNumSlotRows() == 1);

    h.chain.setOwner (mixerTrack);
    REQUIRE (h.chain.getNumSlotRows() == 2);
    REQUIRE (h.chain.getSelectedSlot() == 0);

    // And back, without carrying the other chain's selection into it.
    h.chain.setOwner (h.channel());
    REQUIRE (h.chain.getNumSlotRows() == 1);
    REQUIRE (h.chain.getSelectedSlot() == 0);
}

TEST_CASE ("the editor never selects a slot that is not there", "[effects][ui]")
{
    // Deleting the selected slot used to be the obvious way to leave the
    // parameter area pointing at a removed effect.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    h.chain.selectSlot (2);
    REQUIRE (h.chain.getSelectedSlot() == 2);

    juce::UndoManager& undo = h.document.getUndoManager();
    juce::Array<juce::ValueTree> effects;

    for (const auto& child : h.channel())
        if (child.hasType (ids::EFFECT))
            effects.add (child);

    ProjectEdits::removeEffect (h.channel(), effects.getLast(), &undo);

    REQUIRE (h.chain.getNumSlotRows() == 2);
    REQUIRE (h.chain.getSelectedSlot() < h.chain.getNumSlotRows());

    // Asking for a slot beyond the end clamps rather than going out of range.
    h.chain.selectSlot (99);
    REQUIRE (h.chain.getSelectedSlot() == 1);

    h.chain.selectSlot (-5);
    REQUIRE (h.chain.getSelectedSlot() == 0);
}

TEST_CASE ("the add button stops at a full chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    for (int i = 0; i < kMaxEffectsPerChain; ++i)
        h.chain.addEffectOfType ("filter");

    REQUIRE (h.chain.getNumSlotRows() == kMaxEffectsPerChain);

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == kMaxEffectsPerChain);
    REQUIRE (h.typesInOrder().size() == kMaxEffectsPerChain);
}

TEST_CASE ("an editor pointed at nothing is empty rather than stale", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == 1);

    h.chain.setOwner ({});
    REQUIRE (h.chain.getNumSlotRows() == 0);

    // And adding into nothing does not throw or write anywhere.
    h.chain.addEffectOfType ("delay");
    REQUIRE (h.chain.getNumSlotRows() == 0);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb" });
}

TEST_CASE ("cards expand and collapse independently", "[effects][ui]")
{
    // The old editor showed one slot's parameters at a time, so comparing a
    // filter's cutoff against a delay's time meant clicking between them.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    REQUIRE (h.chain.getNumSlotRows() == 3);

    // Adding an effect opens it: you added it to set it up.
    REQUIRE (h.chain.isSlotExpanded (2));

    h.chain.setSlotExpanded (0, true);
    h.chain.setSlotExpanded (1, true);

    REQUIRE (h.chain.isSlotExpanded (0));
    REQUIRE (h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (2));

    const auto allOpen = h.chain.getRequiredHeight();

    h.chain.setSlotExpanded (1, false);
    REQUIRE (! h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (0));
    REQUIRE (h.chain.isSlotExpanded (2));

    // Closing one makes the chain shorter, which is what the host scrolls.
    REQUIRE (h.chain.getRequiredHeight() < allOpen);
}

TEST_CASE ("which cards are open survives a rebuild, and follows the effect", "[effects][ui]")
{
    // Expansion is keyed on the effect id, not its position, so reordering a
    // chain does not shuffle which cards are open.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    h.chain.setSlotExpanded (0, true);
    h.chain.setSlotExpanded (1, false);
    h.chain.setSlotExpanded (2, false);

    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });
    REQUIRE (h.chain.isSlotExpanded (0));

    // Move the open filter to the end.
    h.chain.moveSlot (0, 2);

    REQUIRE (h.typesInOrder() == juce::StringArray { "delay", "reverb", "filter" });
    REQUIRE (! h.chain.isSlotExpanded (0));
    REQUIRE (! h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (2));      // still the filter

    // And an unrelated document change does not close anything.
    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addChannel (h.document.getState(), "Extra", &undo);
    REQUIRE (h.chain.isSlotExpanded (2));
}

TEST_CASE ("expansion is view state, not document state", "[effects][ui]")
{
    // Opening a card must not put anything on the undo stack or make the
    // project dirty - it is not a change to the music.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");

    const auto before = ProjectSerializer::toJsonString (h.document.getState());

    h.document.getUndoManager().clearUndoHistory();
    h.chain.setSlotExpanded (0, false);
    h.chain.setSlotExpanded (0, true);

    REQUIRE (! h.document.getUndoManager().canUndo());
    REQUIRE (ProjectSerializer::toJsonString (h.document.getState()) == before);
}

TEST_CASE ("dragging a card by its grip reorders the chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });

    // moveSlot is what the grip drives, and what the up/down buttons drive too.
    h.chain.moveSlot (2, 0);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb", "filter", "delay" });

    // Out-of-range targets clamp rather than dropping the effect.
    h.chain.moveSlot (0, 99);
    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });

    h.chain.moveSlot (2, -5);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb", "filter", "delay" });
}

TEST_CASE ("a frequency field drags by ratio, not by hertz", "[effects][ui]")
{
    // A cutoff over 20 to 18000 Hz dragged linearly gives about 70 Hz per
    // pixel, so the whole musically useful low end is the first three pixels.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewNumberField linear, logarithmic;

    for (auto* field : { &linear, &logarithmic })
    {
        field->setRange (20.0, 18000.0, 1.0);
        field->setValue (1000.0, juce::dontSendNotification);
        field->setSize (90, 40);
    }

    logarithmic.setLogarithmic (true);

    const auto dragBy = [] (DewNumberField& field, int pixels)
    {
        const juce::Point<float> start { 45.0f, 20.0f };
        const auto end = start.translated (0.0f, (float) -pixels);

        const auto make = [&field] (juce::Point<float> position, juce::Point<float> down)
        {
            return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(),
                                      position, juce::ModifierKeys(),
                                      1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &field, &field,
                                      juce::Time::getCurrentTime(), down,
                                      juce::Time::getCurrentTime(), 1, false };
        };

        field.mouseDown (make (start, start));
        field.mouseDrag (make (end, start));
        field.mouseUp (make (end, start));
    };

    dragBy (linear, 10);
    dragBy (logarithmic, 10);

    INFO ("linear " << linear.getValue() << " logarithmic " << logarithmic.getValue());

    // Ten pixels up is a huge jump linearly and a musical interval on a log
    // taper - which is the whole point.
    REQUIRE (linear.getValue() > 1600.0);
    REQUIRE (logarithmic.getValue() < 1400.0);
    REQUIRE (logarithmic.getValue() > 1000.0);
}
