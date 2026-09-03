#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "lang/Grid.h"
#include "lang/Rng.h"

using namespace dew::lang;

TEST_CASE ("a duration literal reduces to an exact rational", "[score][grid]")
{
    struct Case
    {
        const char* text;
        int numerator;
        int denominator;
    };

    const Case cases[] = {
        { "1/4", 1, 4 },    { "1/8", 1, 8 },   { "1/1", 1, 1 },
        { "3/8", 3, 8 },    { "1/4.", 3, 8 },  // dotted quarter is three eighths
        { "1/2.", 3, 4 },   { "1/8t", 1, 12 }, // eighth triplet is a twelfth - no double says so
        { "1/16t", 1, 24 }, { "1/4t", 1, 6 },  { "2/4", 1, 2 }, // reduced
    };

    for (const auto& c : cases)
    {
        INFO ("duration " << c.text);
        const auto parsed = parseDuration (c.text);
        REQUIRE (parsed.has_value());
        REQUIRE (parsed->numerator == c.numerator);
        REQUIRE (parsed->denominator == c.denominator);
    }
}

TEST_CASE ("what is not a duration is refused rather than guessed", "[score][grid]")
{
    for (const auto* text :
         { "", "1", "/4", "1/", "1/0", "0/4", "1/4x", "a/4", "1/4.t.", "1..4", "-1/4" })
    {
        INFO ("text " << text);
        REQUIRE_FALSE (parseDuration (text).has_value());
    }
}

TEST_CASE ("a meter is a ratio but never dotted or tripleted", "[score][grid]")
{
    const auto common = parseTimeSignature ("4/4");
    REQUIRE (common.has_value());
    REQUIRE (common->beatsPerBar == 4);
    REQUIRE (common->beatUnit == 4);

    const auto compound = parseTimeSignature ("6/8");
    REQUIRE (compound.has_value());
    REQUIRE (compound->beatsPerBar == 6);
    REQUIRE (compound->beatUnit == 8);

    // A meter is NOT reduced - 2/4 and 4/8 are different meters, unlike
    // durations where they are the same length.
    const auto two = parseTimeSignature ("2/4");
    REQUIRE (two.has_value());
    REQUIRE (two->beatsPerBar == 2);
    REQUIRE (two->beatUnit == 4);

    REQUIRE_FALSE (parseTimeSignature ("1/4.").has_value());
    REQUIRE_FALSE (parseTimeSignature ("1/8t").has_value());
}

TEST_CASE ("the grid one duration needs is the published table", "[score][grid]")
{
    // The table the whole quantisation story rests on, at beatUnit 4.
    struct Case
    {
        const char* text;
        int needed;
    };

    const Case cases[] = {
        { "1/4", 1 },   { "1/8", 2 },  { "1/4.", 2 }, { "1/8t", 3 }, { "1/16", 4 },
        { "1/16t", 6 }, { "1/32", 8 }, { "1/1", 1 },  { "1/2", 1 },
    };

    for (const auto& c : cases)
    {
        INFO ("duration " << c.text);
        const auto duration = parseDuration (c.text);
        REQUIRE (duration.has_value());
        REQUIRE (gridNeededFor (*duration, 4) == c.needed);
    }
}

TEST_CASE ("sixteenths and eighth triplets meet at twelve", "[score][grid]")
{
    // The claim the language's default grid rests on: 12 is the sweet spot,
    // giving both sixteenths and eighth-note triplets exactly.
    const std::vector<DurationUse> uses {
        { *parseDuration ("1/16"), { 0, 4 } },
        { *parseDuration ("1/8t"), { 10, 14 } },
    };

    const auto resolved = resolveGrid (uses, 4);

    REQUIRE (resolved.required == 12);
    REQUIRE (resolved.stepsPerBeat == 12);
    REQUIRE_FALSE (resolved.exceedsHostLimit);

    // And at 12 both are whole numbers of steps.
    REQUIRE (stepsFor (*parseDuration ("1/16"), 4, 12) == 3);
    REQUIRE (stepsFor (*parseDuration ("1/8t"), 4, 12) == 4);
    REQUIRE (stepsFor (*parseDuration ("1/4"), 4, 12) == 12);
    REQUIRE (stepsFor (*parseDuration ("1/8"), 4, 12) == 6);
    REQUIRE (stepsFor (*parseDuration ("1/16t"), 4, 12) == 2);
}

TEST_CASE ("a thirty-second against a triplet does not fit and says why", "[score][grid]")
{
    // 1/32 wants 8, 1/8t wants 3, lcm is 24 - and dew stores at most 16.
    // The message has to name BOTH durations, because the conflict is between
    // them and either one alone would have been fine.
    const std::vector<DurationUse> uses {
        { *parseDuration ("1/8"), { 0, 3 } },
        { *parseDuration ("1/32"), { 10, 14 } },
        { *parseDuration ("1/8t"), { 40, 44 } },
    };

    const auto resolved = resolveGrid (uses, 4);

    REQUIRE (resolved.required == 24);
    REQUIRE (resolved.exceedsHostLimit);

    REQUIRE (resolved.firstWitness.has_value());
    REQUIRE (resolved.secondWitness.has_value());

    // The witnesses are the conflicting PAIR, not the two largest: 1/8 wants 2
    // and divides 8, so it is not part of the problem.
    REQUIRE (resolved.firstWitness->range == SourceRange { 10, 14 });
    REQUIRE (resolved.secondWitness->range == SourceRange { 40, 44 });
}

TEST_CASE ("a single duration can be the whole requirement on its own", "[score][grid]")
{
    const std::vector<DurationUse> uses {
        { *parseDuration ("1/4"), { 0, 3 } },
        { *parseDuration ("1/16t"), { 10, 15 } },
    };

    const auto resolved = resolveGrid (uses, 4);

    REQUIRE (resolved.required == 6);
    REQUIRE_FALSE (resolved.exceedsHostLimit);

    // 1/16t alone needs 6, so there is one witness and no pair to blame.
    REQUIRE (resolved.firstWitness.has_value());
    REQUIRE (resolved.firstWitness->range == SourceRange { 10, 15 });
    REQUIRE_FALSE (resolved.secondWitness.has_value());
}

TEST_CASE ("the meter's denominator changes what fits", "[score][grid]")
{
    // In 6/8 a beat IS an eighth, so an eighth costs one step where in 4/4 it
    // cost two. A grid rule that ignored beatUnit would be wrong for every
    // compound meter.
    const auto eighth = *parseDuration ("1/8");

    REQUIRE (gridNeededFor (eighth, 4) == 2);
    REQUIRE (gridNeededFor (eighth, 8) == 1);

    REQUIRE (stepsFor (eighth, 8, 4) == 4);
    REQUIRE (stepsFor (eighth, 4, 4) == 2);
}

TEST_CASE ("nothing to reconcile is a grid of one", "[score][grid]")
{
    const auto resolved = resolveGrid ({}, 4);

    REQUIRE (resolved.required == 1);
    REQUIRE_FALSE (resolved.exceedsHostLimit);
    REQUIRE_FALSE (resolved.firstWitness.has_value());
}

// ------------------------------------------------------------------------------

TEST_CASE ("the generators reproduce fixed values", "[score][rng]")
{
    // The portability guard. These numbers are the file format: if a future
    // change to Rng moves them, every seeded score renders differently, and if
    // someone reaches for std::uniform_int_distribution this is what catches it
    // before CI on another platform does.
    // splitmix64's first two are the reference implementation's published
    // outputs for seeds 0 and 1, so this also pins that it IS splitmix64 and
    // not something close to it.
    REQUIRE (splitmix64 (0) == 0xE220A8397B1DCDAFULL);
    REQUIRE (splitmix64 (1) == 0x910A2DEC89025CC1ULL);
    REQUIRE (splitmix64 (0x5EEDC0FFEEULL) == 0xE8D155B9E94619BBULL);

    REQUIRE (fnv1a64 ("") == 0xCBF29CE484222325ULL);
    REQUIRE (fnv1a64 ("section:verse") == 0xEB3A45C891BD1897ULL);

    Rng rng { 12345 };
    REQUIRE (rng.nextBits() == 2828472903u);
    REQUIRE (rng.nextBits() == 4192029128u);
    REQUIRE (rng.nextBits() == 2830763162u);
}

TEST_CASE ("a bounded draw stays in range and uses all of it", "[score][rng]")
{
    Rng rng { 99 };
    std::set<std::uint32_t> seen;

    for (int i = 0; i < 4000; ++i)
    {
        const auto value = rng.below (7);
        REQUIRE (value < 7u);
        seen.insert (value);
    }

    // Every value reachable - a bounded draw that silently never returns its
    // top value is the classic modulo-bias bug.
    REQUIRE (seen.size() == 7);

    // A degenerate bound is total, not undefined.
    REQUIRE (rng.below (0) == 0u);
    REQUIRE (rng.below (1) == 0u);
}

TEST_CASE ("a unit float stays inside its half-open range", "[score][rng]")
{
    Rng rng { 7 };

    for (int i = 0; i < 5000; ++i)
    {
        const auto value = rng.unitFloat();
        REQUIRE (value >= 0.0f);
        REQUIRE (value < 1.0f);
    }
}

TEST_CASE ("a seed path depends on its ancestors and its own label only", "[score][rng]")
{
    const SeedPath song { 0x5EEDC0FFEEULL };

    const auto verse = song.child ("section:verse");
    const auto chorus = song.child ("section:chorus");

    REQUIRE (verse.value() != chorus.value());

    // Reaching the same place by the same route gives the same key, every time.
    REQUIRE (song.child ("section:verse").value() == verse.value());

    // A sibling's existence cannot change a key: chorus was derived without
    // verse being consulted, and vice versa.
    const SeedPath fresh { 0x5EEDC0FFEEULL };
    REQUIRE (fresh.child ("section:chorus").value() == chorus.value());

    // Descending further keeps them apart.
    const auto verseLead = verse.child ("channel:lead");
    const auto chorusLead = chorus.child ("channel:lead");
    REQUIRE (verseLead.value() != chorusLead.value());

    // Instances are indexed without building a string, and each is distinct.
    std::set<std::uint64_t> instances;

    for (int i = 0; i < 32; ++i)
        instances.insert (verse.child ("instance", i).value());

    REQUIRE (instances.size() == 32);

    // And an ordinal is not the same as the bare label.
    REQUIRE (verse.child ("instance", 0).value() != verse.child ("instance").value());
}

TEST_CASE ("changing the song seed changes every path below it", "[score][rng]")
{
    const SeedPath a { 1 };
    const SeedPath b { 2 };

    REQUIRE (a.value() != b.value());
    REQUIRE (a.child ("section:verse").child ("channel:lead").value()
             != b.child ("section:verse").child ("channel:lead").value());
}
