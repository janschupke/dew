#include <cmath>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/Transport.h"

using namespace dew;
using Catch::Approx;

TEST_CASE ("a step is the expected number of samples", "[transport]")
{
    // 120 bpm, 16ths, 48 kHz: a beat is 0.5 s = 24000 samples, so a 16th is 6000.
    REQUIRE (Transport::samplesPerStepFor (120.0, 4, 48000.0) == Approx (6000.0));

    // 8ths are twice as long as 16ths.
    REQUIRE (Transport::samplesPerStepFor (120.0, 2, 48000.0) == Approx (12000.0));

    // Halving the tempo doubles the step.
    REQUIRE (Transport::samplesPerStepFor (60.0, 4, 48000.0) == Approx (12000.0));

    // 44.1 kHz, 124 bpm - the demo project's settings.
    REQUIRE (Transport::samplesPerStepFor (124.0, 4, 44100.0) == Approx (5334.677).epsilon (0.0001));
}

TEST_CASE ("degenerate tempos and step divisions do not produce nonsense", "[transport]")
{
    REQUIRE (Transport::samplesPerStepFor (0.0, 4, 44100.0) > 0.0);
    REQUIRE (Transport::samplesPerStepFor (-100.0, 4, 44100.0) > 0.0);
    REQUIRE (Transport::samplesPerStepFor (120.0, 0, 44100.0) > 0.0);
    REQUIRE (Transport::samplesPerStepFor (1e9, 4, 44100.0) > 0.0);
}

TEST_CASE ("the playhead does not drift over a long run", "[transport]")
{
    Transport transport;
    transport.prepare (44100.0);
    transport.setTempo (128.0, 4);

    // Ten minutes in 512-sample blocks. Because position is an integer sample
    // count rather than an accumulated float, this has to be exact.
    constexpr int blockSize = 512;
    constexpr int blocks = 51680;

    for (int i = 0; i < blocks; ++i)
        transport.advance (blockSize);

    REQUIRE (transport.getPositionSamples() == (juce::int64) blockSize * blocks);
}

TEST_CASE ("the playhead wraps at the loop point", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);      // 6000 samples per step
    transport.setLoopLengthSteps (16);  // 96000 samples per loop

    transport.advance (96000);
    REQUIRE (transport.getPositionSamples() == 0);

    transport.advance (96001);
    REQUIRE (transport.getPositionSamples() == 1);
}

TEST_CASE ("a block longer than the loop still lands in range", "[transport]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopLengthSteps (4);   // 24000 samples

    transport.advance (24000 * 5 + 7);

    REQUIRE (transport.getPositionSamples() >= 0);
    REQUIRE (transport.getPositionSamples() < 24000);
    REQUIRE (transport.getPositionSamples() == 7);
}

// --- the loop window ---------------------------------------------------------

TEST_CASE ("a loop range wraps back to its start, not to zero", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);      // 6000 samples per step
    transport.setLoopRange (4.0, 8.0);  // [24000, 48000)

    transport.setPositionSamples (24000);
    transport.advance (24000);

    // The whole point of a range: the old length-at-zero form would have put
    // this at 0, which is a bar and a half before where the loop begins.
    REQUIRE (transport.getPositionSamples() == 24000);

    transport.advance (24001);
    REQUIRE (transport.getPositionSamples() == 24001);
}

TEST_CASE ("a block longer than the loop range still lands inside it", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopRange (4.0, 8.0);

    transport.setPositionSamples (24000);
    transport.advance (24000 * 5 + 7);

    REQUIRE (transport.getPositionSamples() >= 24000);
    REQUIRE (transport.getPositionSamples() < 48000);
    REQUIRE (transport.getPositionSamples() == 24007);
}

TEST_CASE ("a playhead before a loop plays into it rather than snapping", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopRange (4.0, 8.0);

    // Enabling a loop four bars ahead must not teleport the music there.
    transport.setPositionSamples (0);
    transport.advance (6000);
    REQUIRE (transport.getPositionSamples() == 6000);

    transport.advance (6000);
    REQUIRE (transport.getPositionSamples() == 12000);

    // It is only confined once it has actually run past the end.
    for (int i = 0; i < 20; ++i)
        transport.advance (6000);

    REQUIRE (transport.getPositionSamples() >= 24000);
    REQUIRE (transport.getPositionSamples() < 48000);
}

TEST_CASE ("a zero-width or reversed loop range is free-running", "[transport][loop]")
{
    for (const auto range : { std::pair { 8.0, 8.0 }, std::pair { 8.0, 4.0 } })
    {
        Transport transport;
        transport.prepare (48000.0);
        transport.setTempo (120.0, 4);
        transport.setLoopRange (range.first, range.second);

        INFO ("range " << range.first << " -> " << range.second);
        REQUIRE_FALSE (transport.hasLoop());

        for (int i = 0; i < 10; ++i)
            transport.advance (6000);

        // Reversed is NOT swapped: one rule here, and backwards means no loop.
        REQUIRE (transport.getPositionSamples() == 60000);
    }
}

TEST_CASE ("setLoopLengthSteps is a loop range anchored at zero", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopLengthSteps (16);

    REQUIRE (transport.getLoopStartSteps() == 0.0);
    REQUIRE (transport.getLoopEndSteps() == 16.0);
    REQUIRE (transport.hasLoop());

    // The same two assertions the length-based case above makes, so the compat
    // shim is pinned to the behaviour it is standing in for.
    transport.advance (96000);
    REQUIRE (transport.getPositionSamples() == 0);

    transport.advance (96001);
    REQUIRE (transport.getPositionSamples() == 1);
}

TEST_CASE ("a negative loop start is clamped to the top", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopRange (-4.0, 8.0);

    REQUIRE (transport.getLoopStartSteps() == 0.0);
    REQUIRE (transport.getLoopEndSteps() == 8.0);

    transport.setPositionSamples (48000);
    transport.wrapIntoLoop();
    REQUIRE (transport.getPositionSamples() == 0);
}

TEST_CASE ("the loop window follows the tempo", "[transport][loop]")
{
    Transport transport;
    transport.prepare (48000.0);
    transport.setTempo (120.0, 4);
    transport.setLoopRange (0.0, 4.0);

    REQUIRE (transport.loopEndSamples() == 24000);

    // Stored in steps and converted per call, so a tempo change cannot leave a
    // stale sample count behind.
    transport.setTempo (60.0, 4);
    REQUIRE (transport.loopEndSamples() == 48000);
}

TEST_CASE ("the loop ends round where the ruler draws them", "[transport][loop]")
{
    Transport transport;
    transport.prepare (44100.0);
    transport.setTempo (124.0, 4);   // ~5334.677 samples per step: not a whole number

    transport.setLoopRange (4.0, 8.0);

    const auto sps = transport.samplesPerStep();
    const auto expected = (juce::int64) std::llround (sps * 8.0)
                        - (juce::int64) std::llround (sps * 4.0);

    // NOT llround (sps * (8 - 4)). The two differ by a sample here, and only
    // this one puts the loop's ends where the ruler puts its bar lines.
    REQUIRE (transport.loopEndSamples() - transport.loopStartSamples() == expected);

    transport.setPositionSamples (transport.loopStartSamples());
    transport.advance ((int) expected);
    REQUIRE (transport.getPositionSamples() == transport.loopStartSamples());
}
