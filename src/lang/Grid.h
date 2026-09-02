#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "lang/SourceRange.h"

namespace dew::lang
{

/** dew stores a note's start and length as an INTEGER count of steps, and a step
    is 1/stepsPerBeat of a beat. `stepsPerBeat` runs 1..16 and there is no
    negotiating with that: Meter::stepsPerBeat is what Transport divides by.

    So every duration the language can write has to land on a whole number of
    steps, and which grid makes that true is a property of the durations
    themselves. This is where that is worked out.
*/
inline constexpr int maxStepsPerBeat = 16;

/** A duration as an exact rational of a WHOLE note.

    Rational rather than a float because the whole point is exactness: 1/8t is
    1/12 and no double will say so. Dots and triplets are folded in at parse
    time, so a Duration is always already reduced.
*/
struct Duration
{
    int numerator = 1;
    int denominator = 4;

    constexpr bool operator== (const Duration& other) const noexcept
    {
        return numerator == other.numerator && denominator == other.denominator;
    }

    constexpr bool operator!= (const Duration& other) const noexcept
    {
        return ! operator== (other);
    }
};

/** Parses `1/4`, `1/4.`, `1/8t`, `3/8`, `1/1`. Returns nothing if the lexeme is
    not a note value at all - `4/4` parses fine here and is only a meter because
    of where it appeared, which is the parser's business, not this function's.
*/
std::optional<Duration> parseDuration (std::string_view lexeme) noexcept;

/** Parses `4/4` as a time signature. Same lexeme shape as a duration; the
    difference is entirely contextual, so both live here and the caller picks.
*/
struct TimeSignature
{
    int beatsPerBar = 4;
    int beatUnit = 4;
};

std::optional<TimeSignature> parseTimeSignature (std::string_view lexeme) noexcept;

/** Steps per beat this one duration needs, given the meter's denominator.

    steps = numerator * beatUnit * stepsPerBeat / denominator, so the requirement
    is denominator / gcd(denominator, numerator * beatUnit). At beatUnit 4:
    1/4 -> 1, 1/8 -> 2, 1/4. -> 2, 1/8t -> 3, 1/16 -> 4, 1/16t -> 6, 1/32 -> 8.
*/
int gridNeededFor (Duration, int beatUnit) noexcept;

/** Whole steps for a duration on a given grid. Exact only when the grid is a
    multiple of gridNeededFor; callers check that first.
*/
int stepsFor (Duration, int beatUnit, int stepsPerBeat) noexcept;

/** One duration literal, and where it was written - so a grid conflict can name
    both of the durations that caused it rather than only the total.
*/
struct DurationUse
{
    Duration duration;
    SourceRange range;
};

struct GridResolution
{
    int stepsPerBeat = 4;

    /** True when the requirement exceeds what dew can store, in which case
        `stepsPerBeat` is meaningless and the two witnesses say why.
    */
    bool exceedsHostLimit = false;

    /** The two durations whose requirements multiply out to the total - what a
        diagnostic points at. Empty when there was nothing to reconcile.
    */
    std::optional<DurationUse> firstWitness;
    std::optional<DurationUse> secondWitness;

    /** The requirement, even when it exceeds the limit, so the message can say
        what it would have taken.
    */
    int required = 1;
};

/** The least grid on which every one of these durations is exact.

    Reports the two witnesses whose requirements combine to produce the answer,
    picked as the pair whose lcm IS the total - which is the pair a user has to
    change, rather than the largest two, which often are not.
*/
GridResolution resolveGrid (const std::vector<DurationUse>&, int beatUnit) noexcept;

} // namespace dew::lang
