#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/MidiRouter.h"
#include "model/ProjectFactory.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** An engine that has actually been prepared and given a project, so a routed
    note reaches a real voice rather than a channel index that resolves to
    nothing. Rendering a block is how the tests below observe what happened -
    the queues are drained on the audio thread, so nothing is visible until one
    runs.
*/
struct RouterHarness
{
    RouterHarness()
    {
        engine.prepare (sampleRate, blockSize);
        engine.setProject (ProjectFactory::createDefault());
        router.setTargetChannel (channelIndex);
        render();   // let the first snapshot land
    }

    /** Renders one block and returns its peak. */
    float render()
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        engine.processBlock (buffer);
        return buffer.getMagnitude (0, 0, buffer.getNumSamples());
    }

    /** Renders several blocks, returning the loudest. A note-on needs one block
        to be drained and another to be audible through its attack.
    */
    float renderBlocks (int count)
    {
        auto peak = 0.0f;

        for (int i = 0; i < count; ++i)
            peak = juce::jmax (peak, render());

        return peak;
    }

    /** Renders `count` blocks and returns the peak of the LAST one.

        Distinct from renderBlocks, and the distinction matters: a note that has
        just been released is still loud in the blocks immediately after, so the
        loudest block over a span says nothing about whether it ended. What a
        release test wants is the level once the span is over.
    */
    float peakAfter (int count)
    {
        auto peak = 0.0f;

        for (int i = 0; i < count; ++i)
            peak = render();

        return peak;
    }

    float bend() const  { return engine.getChannelBend (channelIndex); }
    float mod() const   { return engine.getChannelModulation (channelIndex); }

    static constexpr double sampleRate = 44100.0;
    static constexpr int blockSize = 256;

    /** "Bass": a saw with sustain 0.35, so a held note stays audible for as
        long as it is held. Channel 0 is the kick, whose sustain is 0 - a note
        there decays to nothing on its own and can prove nothing about holding.
    */
    static constexpr int channelIndex = 2;

    AudioEngine engine;
    MidiRouter router { engine };
};

juce::MidiMessage noteOn (int channel, int pitch, juce::uint8 velocity)
{
    return juce::MidiMessage::noteOn (channel, pitch, velocity);
}

juce::MidiMessage noteOff (int channel, int pitch)
{
    return juce::MidiMessage::noteOff (channel, pitch);
}

juce::MidiMessage cc (int channel, int number, int value)
{
    return juce::MidiMessage::controllerEvent (channel, number, value);
}

} // namespace

TEST_CASE ("a routed note reaches the engine and is heard", "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (noteOn (1, 60, 100));

    REQUIRE (h.renderBlocks (4) > 0.0f);
}

TEST_CASE ("a note on with velocity zero is a note off", "[midi][router]")
{
    // Real hardware sends these constantly. Treating them as note-ons leaves
    // every key ringing forever, which is the bug this pins.
    RouterHarness h;

    h.router.handleMessage (noteOn (1, 60, 100));
    REQUIRE (h.renderBlocks (4) > 0.0f);

    h.router.handleMessage (noteOn (1, 60, 0));

    // The release still has a tail, so it must fall rather than reach silence
    // immediately - what matters is that it is falling at all.
    const auto duringRelease = h.renderBlocks (2);
    const auto later = h.renderBlocks (40);

    INFO ("release " << duringRelease << " -> " << later);
    REQUIRE (later < duringRelease);
}

TEST_CASE ("omni passes every channel and a filter passes only its own", "[midi][router]")
{
    RouterHarness h;

    REQUIRE (h.router.getChannelFilter() == MidiRouter::omni);

    for (int channel = 1; channel <= 16; ++channel)
    {
        const auto before = h.router.getActivityCount();
        h.router.handleMessage (noteOn (channel, 60, 100));
        REQUIRE (h.router.getActivityCount() == before + 1);
        h.router.handleMessage (noteOff (channel, 60));
    }

    h.router.setChannelFilter (3);

    const auto before = h.router.getActivityCount();

    h.router.handleMessage (noteOn (1, 60, 100));
    h.router.handleMessage (noteOn (2, 60, 100));
    REQUIRE (h.router.getActivityCount() == before);

    h.router.handleMessage (noteOn (3, 60, 100));
    REQUIRE (h.router.getActivityCount() == before + 1);
}

TEST_CASE ("the sustain pedal holds a note past its key release", "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (cc (1, 64, 127));           // pedal down
    h.router.handleMessage (noteOn (1, 60, 100));

    const auto sounding = h.peakAfter (40);             // past attack and decay
    REQUIRE (sounding > 0.0f);

    // Key up, pedal still down: the note must keep going.
    h.router.handleMessage (noteOff (1, 60));
    const auto held = h.peakAfter (40);

    INFO ("sounding " << sounding << ", held " << held);
    REQUIRE (held > 0.0f);

    h.router.handleMessage (cc (1, 64, 0));             // pedal up
    const auto afterRelease = h.peakAfter (60);

    INFO ("after the pedal lifted: " << afterRelease);
    REQUIRE (afterRelease < held * 0.5f);
}

TEST_CASE ("the sustain threshold is the spec's half-way point", "[midi][router]")
{
    // 64 is down, 63 is up. A pedal that reports 64 at rest would otherwise
    // sustain everything forever.
    RouterHarness h;

    REQUIRE (MidiRouter::sustainThreshold == 64);

    h.router.handleMessage (cc (1, 64, 63));          // 63 is up
    h.router.handleMessage (noteOn (1, 60, 100));
    REQUIRE (h.peakAfter (40) > 0.0f);

    h.router.handleMessage (noteOff (1, 60));
    const auto notHeld = h.peakAfter (60);

    h.router.handleMessage (cc (1, 64, 64));          // 64 is down
    h.router.handleMessage (noteOn (1, 62, 100));
    REQUIRE (h.peakAfter (40) > 0.0f);
    h.router.handleMessage (noteOff (1, 62));
    const auto held = h.peakAfter (40);

    INFO ("not held " << notHeld << ", held " << held);
    REQUIRE (held > notHeld);
}

TEST_CASE ("all notes off silences everything and returns the controllers to rest",
           "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (cc (1, 64, 127));
    h.router.handleMessage (noteOn (1, 60, 100));
    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 16383));
    h.router.handleMessage (cc (1, 1, 127));

    REQUIRE (h.renderBlocks (4) > 0.0f);
    REQUIRE_FALSE (juce::exactlyEqual (h.bend(), 0.0f));
    REQUIRE (h.mod() > 0.0f);

    h.router.handleMessage (cc (1, 123, 0));
    h.render();

    REQUIRE (juce::exactlyEqual (h.bend(), 0.0f));
    REQUIRE (juce::exactlyEqual (h.mod(), 0.0f));

    // And the sustained note is gone with it, rather than being resurrected
    // when the pedal next lifts.
    const auto after = h.peakAfter (80);
    INFO ("tail after all notes off: " << after);
    REQUIRE (after < 0.05f);
}

TEST_CASE ("all sound off is treated like all notes off", "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 16383));
    h.router.handleMessage (cc (1, 120, 0));
    h.render();

    REQUIRE (juce::exactlyEqual (h.bend(), 0.0f));
}

TEST_CASE ("transpose shifts a note and drops one pushed off the end", "[midi][router]")
{
    RouterHarness h;

    h.router.setTranspose (12);
    REQUIRE (h.router.getTranspose() == 12);

    h.router.handleMessage (noteOn (1, 60, 100));
    REQUIRE ((h.router.getLastNote() & 0xff) == 72);

    // Off the top: dropped, never wrapped. Wrapping would answer a key at the
    // top of the keyboard with a note at the bottom.
    const auto before = h.router.getActivityCount();
    h.router.handleMessage (noteOn (1, 120, 100));
    REQUIRE (h.router.getActivityCount() == before);
    REQUIRE ((h.router.getLastNote() & 0xff) == 72);

    h.router.setTranspose (-12);
    const auto beforeLow = h.router.getActivityCount();
    h.router.handleMessage (noteOn (1, 5, 100));
    REQUIRE (h.router.getActivityCount() == beforeLow);
}

TEST_CASE ("transpose is clamped to the range the panel offers", "[midi][router]")
{
    RouterHarness h;

    h.router.setTranspose (999);
    REQUIRE (h.router.getTranspose() == MidiRouter::maxTranspose);

    h.router.setTranspose (-999);
    REQUIRE (h.router.getTranspose() == -MidiRouter::maxTranspose);
}

TEST_CASE ("the pitch wheel bends by its range, and its centre is exactly zero",
           "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 8192));
    REQUIRE (juce::exactlyEqual (h.bend(), 0.0f));

    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 16383));
    REQUIRE (h.bend() == Approx (MidiRouter::bendRangeSemitones).margin (0.01));

    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 0));
    REQUIRE (h.bend() == Approx (-MidiRouter::bendRangeSemitones).margin (0.01));
}

TEST_CASE ("the mod wheel maps onto nought to one", "[midi][router]")
{
    RouterHarness h;

    h.router.handleMessage (cc (1, 1, 0));
    REQUIRE (juce::exactlyEqual (h.mod(), 0.0f));

    h.router.handleMessage (cc (1, 1, 127));
    REQUIRE (h.mod() == Approx (1.0f).margin (0.001));

    h.router.handleMessage (cc (1, 1, 64));
    REQUIRE (h.mod() == Approx (0.504f).margin (0.01));
}

TEST_CASE ("changing the target channel releases and unbends the old one", "[midi][router]")
{
    // A channel left bent stays bent forever, and a note left sounding has
    // nothing pointing at it any more.
    RouterHarness h;

    h.router.handleMessage (noteOn (1, 60, 100));
    h.router.handleMessage (juce::MidiMessage::pitchWheel (1, 16383));
    REQUIRE (h.renderBlocks (4) > 0.0f);
    REQUIRE_FALSE (juce::exactlyEqual (h.bend(), 0.0f));

    h.router.setTargetChannel (RouterHarness::channelIndex + 1);
    h.render();

    REQUIRE (juce::exactlyEqual (h.bend(), 0.0f));
    REQUIRE (juce::exactlyEqual (h.mod(), 0.0f));

    const auto after = h.peakAfter (80);
    INFO ("tail after switching channel: " << after);
    REQUIRE (after < 0.05f);
}

TEST_CASE ("a message-thread reset clears the MIDI thread's state on its own side",
           "[midi][router][threading]")
{
    // reset() must not write the note bookkeeping itself: the MIDI thread reads
    // it, and a bitset written from two threads is a plain data race. It asks
    // instead, and the MIDI thread clears its own state next time it wakes.
    RouterHarness h;

    h.router.handleMessage (cc (1, 64, 127));      // pedal down
    h.router.handleMessage (noteOn (1, 60, 100));
    h.router.handleMessage (noteOff (1, 60));      // now held by the pedal

    REQUIRE (h.peakAfter (40) > 0.0f);

    h.router.reset();
    h.render();

    // The next message the MIDI thread sees clears the sustain state, so
    // lifting the pedal afterwards resurrects nothing.
    h.router.handleMessage (cc (1, 64, 0));

    const auto after = h.peakAfter (80);
    INFO ("tail after a message-thread reset: " << after);
    REQUIRE (after < 0.05f);
}

TEST_CASE ("a message-thread reset silences a sounding note", "[midi][router][threading]")
{
    RouterHarness h;

    h.router.handleMessage (noteOn (1, 60, 100));
    REQUIRE (h.peakAfter (40) > 0.0f);

    h.router.reset();

    const auto after = h.peakAfter (80);
    INFO ("tail after reset: " << after);
    REQUIRE (after < 0.05f);
}

TEST_CASE ("messages dew does not play are ignored rather than guessed at", "[midi][router]")
{
    RouterHarness h;

    const auto before = h.router.getActivityCount();

    h.router.handleMessage (juce::MidiMessage::midiClock());
    h.router.handleMessage (juce::MidiMessage::midiStart());
    h.router.handleMessage (juce::MidiMessage::programChange (1, 4));
    h.router.handleMessage (juce::MidiMessage::channelPressureChange (1, 90));
    h.router.handleMessage (juce::MidiMessage::aftertouchChange (1, 60, 90));

    REQUIRE (h.router.getActivityCount() == before);
    REQUIRE (h.render() >= 0.0f);   // and nothing threw
}

TEST_CASE ("a key pressed again while the pedal holds it survives the pedal lifting",
           "[midi][router]")
{
    // Re-pressing a sustained key must take it out of the sustained set, or
    // lifting the pedal kills the note the user is still holding.
    RouterHarness h;

    h.router.handleMessage (cc (1, 64, 127));
    h.router.handleMessage (noteOn (1, 60, 100));
    h.router.handleMessage (noteOff (1, 60));      // released, now sustained
    h.router.handleMessage (noteOn (1, 60, 100));  // pressed again

    REQUIRE (h.peakAfter (40) > 0.0f);

    h.router.handleMessage (cc (1, 64, 0));        // pedal up

    const auto stillSounding = h.peakAfter (30);
    INFO ("after the pedal lifted: " << stillSounding);
    REQUIRE (stillSounding > 0.0f);
}
