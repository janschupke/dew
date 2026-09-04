// How a harmony's chords divide the space they are given.
//
// Split out of ScoreGeneratorTests.cpp, along the Catch2 tags it already
// carried. The fixture is ScoreGenHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <set>
#include <string>

#include "ScoreGenHarness.h"

using namespace dew::lang;
using namespace dew::testing;

TEST_CASE ("weights split exactly the space that is left", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // Two bars of 16 steps = 32 steps, shared 2:1:1.
    const auto spans = layOutHarmony (
        harmonyOf ({ chordFrom ("I", 2), chordFrom ("V", 1), chordFrom ("vi", 1) }),
        Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (lengthsOf (spans) == std::vector<int> { 16, 8, 8 });

    // And the total is EXACT - the property largest-remainder buys.
    const auto lengths = lengthsOf (spans);
    REQUIRE (std::accumulate (lengths.begin(), lengths.end(), 0) == 32);
}

TEST_CASE ("an uneven split loses no steps and gains none", "[score][harmony]")
{
    // 16 steps between three equal chords is 5.33 each. Rounding each
    // independently loses a step; largest-remainder hands it to the earliest.
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (
        harmonyOf ({ chordFrom ("I"), chordFrom ("IV"), chordFrom ("V") }), Key { 0, Mode::major },
        16, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());

    const auto lengths = lengthsOf (spans);
    REQUIRE (std::accumulate (lengths.begin(), lengths.end(), 0) == 16);
    REQUIRE (lengths == std::vector<int> { 6, 5, 5 });
}

TEST_CASE ("absolute lengths are taken out before the weights share", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // 4 bars = 64 steps. `I` takes 2 bars outright; the rest share 32.
    const auto spans = layOutHarmony (
        harmonyOf ({ chordFrom ("I", 0, 2), chordFrom ("IV", 1), chordFrom ("V", 1) }),
        Key { 0, Mode::major }, 64, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (lengthsOf (spans) == std::vector<int> { 32, 16, 16 });
}

TEST_CASE ("a harmony longer than its section names the chord that overran", "[score][harmony]")
{
    const std::string source = "I IV V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 0, 1), chordFrom ("IV", 0, 1),
                                           chordFrom ("V", 0, 1) };
    chords[0].range = { 0, 1 };
    chords[1].range = { 2, 4 };
    chords[2].range = { 5, 6 };

    // Three bars of chords in a two-bar section.
    const auto spans = layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 32, 16, 4, 4,
                                      bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());

    const auto& d = bag.all().front();
    REQUIRE (d.code == "E301");

    // The THIRD chord is where it crossed, not the block as a whole.
    REQUIRE (d.primary == SourceRange { 5, 6 });
}

TEST_CASE ("a harmony that leaves a gap is an error, not silent padding", "[score][harmony]")
{
    // Trailing silence nobody asked for is the most expensive kind of bug in
    // generated music, because it sounds plausible.
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 0, 1) }), Key { 0, Mode::major },
                                      64, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());
    REQUIRE (bag.all().front().code == "E302");
}

TEST_CASE ("a chord that would round to nothing is refused", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // Four steps between five chords: one of them gets zero and would simply
    // vanish from the arrangement.
    std::vector<ChordSpec> chords;

    for (int i = 0; i < 5; ++i)
        chords.push_back (chordFrom ("I"));

    const auto spans = layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 4, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());
    REQUIRE (bag.all().front().code == "E303");
}

TEST_CASE ("a bar check that is not on a bar line says where it landed", "[score][harmony]")
{
    // The highest-value error catcher in the harmony syntax: it turns "the
    // section length changed and everything shifted" into one message.
    const std::string source = "I V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 1), chordFrom ("V", 1) };
    chords[0].range = { 0, 1 };
    chords[0].barCheckAfter = true; // asserts a bar line after the first chord
    chords[1].range = { 2, 3 };

    // Three bars shared 1:1 puts the boundary in the middle of bar two.
    layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 48, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());

    const auto& d = bag.all().front();
    REQUIRE (d.code == "E304");
    REQUIRE (d.notes.front().find ("bar 2") != std::string::npos);
}

TEST_CASE ("a bar check that holds says nothing", "[score][harmony]")
{
    const std::string source = "I V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 1), chordFrom ("V", 1) };
    chords[0].barCheckAfter = true;

    layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
}

TEST_CASE ("a tonicised span carries the key its melody should use", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    ChordSpec secondary;
    secondary.symbol = { "V7", 0, "vi" };
    secondary.hasWeight = true;
    secondary.weight = 1;

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), secondary }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (spans.size() == 2);

    REQUIRE (spans[0].localKey.tonicPc == 0);
    REQUIRE (spans[1].localKey.tonicPc == 9); // A, tonicised by V7/vi
    REQUIRE (spans[1].localKey.mode == Mode::harmonicMinor);
}
