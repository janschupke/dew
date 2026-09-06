// =============================================================================
// The LFO settings that reach a note ALREADY SOUNDING.
//
// Separate from OscillatorLfoTests.cpp, which asks what an LFO does to a note
// it started. This file asks what happens when the bank moves UNDER one - an
// automation curve, or a knob turned while a chord is held - and every case
// here therefore renders in real blocks and swaps the bank between two of them.
//
// The trap the sibling file records applies to all of it: one enormous block
// samples the LFO exactly once, at phase zero, and concludes that modulation
// does nothing.
// =============================================================================

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "OscillatorLfoSupport.h"

using namespace dew;
using namespace dewtest;

TEST_CASE ("an LFO depth reaches the note already sounding", "[engine][osc][lfo]")
{
    // Blocks, not one buffer: the pitch fold runs per block, and the swap this
    // test performs only exists between blocks in the first place.
    constexpr auto samples = 16384;
    constexpr auto swapAt = samples / 2;

    const auto loud = bankOf ({ withLfo (classicSlot(), 0.0f, 0.9f, 0.0f) });
    const auto quiet = bankOf ({ withLfo (classicSlot(), 0.0f, 0.1f, 0.0f) });

    const auto held = renderLive (loud, loud, swapAt, samples);
    const auto turned = renderLive (loud, quiet, swapAt, samples);

    // Before the swap the two are the same render, so anything the second half
    // shows is the swap and not a different note.
    for (int i = 0; i < swapAt; ++i)
        REQUIRE (juce::exactlyEqual (held.getSample (0, i), turned.getSample (0, i)));

    auto biggestDifference = 0.0f;

    for (int i = swapAt; i < samples; ++i)
        biggestDifference = juce::jmax (biggestDifference,
                                        std::abs (held.getSample (0, i) - turned.getSample (0, i)));

    CHECK (biggestDifference > 0.01f);
}

TEST_CASE ("an LFO rate reaches the note already sounding, without retriggering",
           "[engine][osc][lfo]")
{
    constexpr auto samples = 16384;
    constexpr auto swapAt = samples / 2;

    const auto slow = bankOf ({ withLfo (classicSlot(), 0.0f, 0.9f, 0.0f, 2.0f) });
    const auto fast = bankOf ({ withLfo (classicSlot(), 0.0f, 0.9f, 0.0f, 9.0f) });

    const auto held = renderLive (slow, slow, swapAt, samples);
    const auto sped = renderLive (slow, fast, swapAt, samples);

    auto biggestDifference = 0.0f;

    for (int i = swapAt; i < samples; ++i)
        biggestDifference = juce::jmax (biggestDifference,
                                        std::abs (held.getSample (0, i) - sped.getSample (0, i)));

    CHECK (biggestDifference > 0.01f);

    // The phase is NOT reset by the change. A retrigger would put the LFO back
    // at the top of its cycle, so the first sample after the swap would jump to
    // the same value the note started from; a sweep continues from where the
    // cycle had got to.
    const auto atStart = sped.getSample (0, 0);
    const auto afterSwap = sped.getSample (0, swapAt);

    CHECK (std::abs (afterSwap - atStart) > 0.001f);
}

TEST_CASE ("a live bank that has not moved renders the bits it always did", "[engine][osc][lfo]")
{
    constexpr auto samples = 16384;

    const auto bank = bankOf ({ withLfo (classicSlot(), 0.4f, 0.6f, 0.0f) });

    const auto withoutLive = renderInBlocks (bank, samples);
    const auto withLive = renderLive (bank, bank, samples, samples);

    // The whole promise of pushing the bank in every block: a project nothing
    // is automating is bit-for-bit the render it was before there was a push.
    for (int i = 0; i < samples; ++i)
        REQUIRE (juce::exactlyEqual (withoutLive.getSample (0, i), withLive.getSample (0, i)));
}

TEST_CASE ("an LFO switched on mid-note waits for the next one", "[engine][osc][lfo]")
{
    constexpr auto samples = 16384;
    constexpr auto swapAt = samples / 2;

    // Note-on with the LFO inactive puts this slot in the PLAIN half of the
    // partition, where there is no OscLfo paired with it. Joining the other
    // half part-way would re-order a float sum every pinned render depends on,
    // so the honest answer is that switching one on reaches the next note - the
    // same answer setFmMatrix gives a matrix switched on.
    const auto off = bankOf ({ classicSlot() });
    const auto on = bankOf ({ withLfo (classicSlot(), 0.0f, 0.9f, 0.0f) });

    const auto stayedOff = renderLive (off, off, swapAt, samples);
    const auto switchedOn = renderLive (off, on, swapAt, samples);

    for (int i = 0; i < samples; ++i)
        REQUIRE (juce::exactlyEqual (stayedOff.getSample (0, i), switchedOn.getSample (0, i)));
}
