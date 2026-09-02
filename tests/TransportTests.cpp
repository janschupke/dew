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
