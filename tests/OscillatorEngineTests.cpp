// Three oscillator slots in the engine, and what they sum to.
//
// Split out of OscillatorTests.cpp along the tags it already carried. The
// shared helpers are OscillatorHarness.h; the engine and UI groups each kept
// the fixture that sat directly above them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/EditorState.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"
#include "OscillatorHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

namespace
{

/** One oscillator slot, as the snapshot carries it. */
OscSettings slotSettings (Waveform wave, float gain = 0.8f, int octave = 0,
                          float detuneCents = 0.0f)
{
    OscSettings s;
    s.enabled = true;
    s.wave = wave;
    s.octave = octave;
    s.detuneCents = detuneCents;
    s.gain = gain;
    return s;
}

OscBankSnapshot bankOf (std::initializer_list<OscSettings> enabledSlots)
{
    OscBankSnapshot bank;

    for (const auto& slot : enabledSlots)
        bank.slots[(size_t) bank.numSlots++] = slot;

    // The rest of the slots exist and are off, as they do in a document.
    while (bank.numSlots < kMaxOscillators)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    bank.anyEnabled = ! std::empty (enabledSlots);
    return bank;
}

/** Renders one note through a real channel, with no effects, no mixer and no
    master gain in the way - so what these tests measure is the oscillators.
*/
juce::AudioBuffer<float> renderOneNote (const OscBankSnapshot& bank, int numSamples = 4096)
{
    SynthChannel channel;
    channel.prepare (44100.0);

    AmpSettings amp;
    amp.attack = 0.0f;
    amp.decay = 0.0f;
    amp.sustain = 1.0f;
    amp.release = 0.0f;

    channel.noteOn (60, 1.0f, bank, amp, numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    channel.renderAdd (buffer.getWritePointer (0), numSamples);

    return buffer;
}

} // namespace

TEST_CASE ("a second oscillator adds to the first", "[engine][oscillator]")
{
    const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
    const auto doubled = renderOneNote (
        bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::sine) }));

    REQUIRE (peakOf (alone) > 0.05f);

    // Summed plainly, each by its own gain: two identical oscillators are twice
    // the level, not the same level shared between them.
    REQUIRE (peakOf (doubled) == Approx (peakOf (alone) * 2.0f).margin (0.01));
}

TEST_CASE ("a switched-off oscillator contributes nothing", "[engine][oscillator]")
{
    auto withSecondOff = bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::saw) });
    withSecondOff.slots[1].enabled = false;

    const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
    const auto off = renderOneNote (withSecondOff);

    REQUIRE (alone.getNumSamples() == off.getNumSamples());

    for (int i = 0; i < alone.getNumSamples(); ++i)
    {
        INFO ("sample " << i);
        REQUIRE (juce::exactlyEqual (alone.getSample (0, i), off.getSample (0, i)));
    }
}

TEST_CASE ("a voice with every oscillator off is exactly silent", "[engine][oscillator]")
{
    OscBankSnapshot bank;

    for (int i = 0; i < kMaxOscillators; ++i)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    // The voice is still allocated and its envelope still runs - there is
    // simply nothing for it to sound. Anything else would make "how many notes
    // are playing" mean "how many are playing audibly".
    REQUIRE (juce::exactlyEqual (peakOf (renderOneNote (bank)), 0.0f));
}

TEST_CASE ("each oscillator carries its own settings into the render", "[engine][oscillator]")
{
    SECTION ("its own waveform")
    {
        const auto sines = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::sine) }));
        const auto mixed = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::square) }));

        bool differs = false;

        for (int i = 0; i < sines.getNumSamples() && ! differs; ++i)
            differs = ! juce::exactlyEqual (sines.getSample (0, i), mixed.getSample (0, i));

        // A start() that latched the first slot's settings into every
        // oscillator would render these identically.
        REQUIRE (differs);
    }

    SECTION ("its own octave")
    {
        const auto unison = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::sine) }));
        const auto anOctaveUp = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::sine, 0.8f, 1) }));

        bool differs = false;

        for (int i = 0; i < unison.getNumSamples() && ! differs; ++i)
            differs = ! juce::exactlyEqual (unison.getSample (0, i), anOctaveUp.getSample (0, i));

        REQUIRE (differs);
    }

    SECTION ("its own detune, which two slots apart beat against each other")
    {
        const auto beating = renderOneNote (
            bankOf (
                { slotSettings (Waveform::sine), slotSettings (Waveform::sine, 0.8f, 0, 8.0f) }),
            44100);

        // Two sines eight cents apart cancel and reinforce over the beat
        // period. The quiet part is what proves they are at different pitches.
        const auto early = beating.getMagnitude (0, 2048);
        const auto quietest = beating.getMagnitude (20000, 2048);

        REQUIRE (early > 0.05f);
        REQUIRE (quietest < early);
    }

    SECTION ("its own gain, which at zero contributes nothing")
    {
        const auto alone = renderOneNote (bankOf ({ slotSettings (Waveform::sine) }));
        const auto withSilentSlot = renderOneNote (
            bankOf ({ slotSettings (Waveform::sine), slotSettings (Waveform::saw, 0.0f) }));

        for (int i = 0; i < alone.getNumSamples(); ++i)
        {
            INFO ("sample " << i);
            REQUIRE (juce::exactlyEqual (alone.getSample (0, i), withSilentSlot.getSample (0, i)));
        }
    }
}

TEST_CASE ("a channel with every oscillator switched off is silent in a real render",
           "[engine][render][oscillator]")
{
    auto project = dew::testing::fixtureProject();

    juce::AudioBuffer<float> sounding;
    REQUIRE (OfflineRenderer::renderToBuffer (project, sounding).ok());
    REQUIRE (peakOf (sounding) > 0.05f);

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL))
            for (int i = 0; i < kMaxOscillators; ++i)
                ProjectEdits::oscillatorAt (channel, i).setProperty (ids::enabled, false, nullptr);

    juce::AudioBuffer<float> silent;
    const auto report = OfflineRenderer::renderToBuffer (project, silent);

    REQUIRE (report.ok());
    REQUIRE (juce::exactlyEqual (peakOf (silent), 0.0f));
}
