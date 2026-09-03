#include "lang/Counterpoint.h"

#include <algorithm>
#include <cmath>

namespace dew::lang
{

namespace
{

/** How many partial lines survive each onset.

    Eight, and the number is not arbitrary: cost is additive along the timeline,
    so the beam is an exact dynamic program over the states it keeps, and eight
    is where widening stopped changing the output on the scores this was tuned
    against. Wider costs time an editor recompiling as you type does not have.
*/
constexpr int beamWidth = 8;

/** Intervals that are consonant, as semitones within an octave.

    Unisons, thirds, fifths, sixths and octaves. The perfect fourth is left out:
    against a lower voice it behaves as a dissonance, which is the whole reason
    the rule is worth having.
*/
bool isConsonant (int semitones)
{
    const auto interval = ((semitones % 12) + 12) % 12;

    return interval == 0 || interval == 3 || interval == 4 || interval == 7 || interval == 8
           || interval == 9;
}

bool isPerfectFifth (int semitones)
{
    return (((semitones % 12) + 12) % 12) == 7;
}

bool isOctaveOrUnison (int semitones)
{
    return (((semitones % 12) + 12) % 12) == 0;
}

int signOf (int value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

/** One partial line, kept between onsets. */
struct Partial
{
    std::vector<int> pitches;
    float cost = 0.0f;
    int previous = -1;
};

/** Everything one transition needs to be judged. */
struct Move
{
    int previousOwn = -1;
    int own = 0;
    int previousOther = -1;
    int other = -1;
    bool strong = false;
    bool ownIsAbove = true;
};

/** True if this move breaks the rule. Independent of whether the rule is hard
    or soft - which of those it is decides what happens next, not what counts as
    breaking it.
*/
bool breaks (CounterpointRule rule, const Move& move)
{
    const auto haveBoth = move.other >= 0;
    const auto haveHistory = move.previousOwn >= 0 && move.previousOther >= 0 && haveBoth;

    const auto interval = haveBoth ? move.own - move.other : 0;
    const auto before = haveHistory ? move.previousOwn - move.previousOther : 0;

    const auto ownStep = haveHistory ? move.own - move.previousOwn : 0;
    const auto otherStep = haveHistory ? move.other - move.previousOther : 0;
    const auto together = haveHistory && signOf (ownStep) != 0
                          && signOf (ownStep) == signOf (otherStep);

    switch (rule)
    {
        case CounterpointRule::parallelFifths:
            return haveHistory && together && isPerfectFifth (interval) && isPerfectFifth (before);

        case CounterpointRule::parallelOctaves:
            return haveHistory && together && isOctaveOrUnison (interval)
                   && isOctaveOrUnison (before);

        case CounterpointRule::directFifths:
            // Both voices moving the SAME way into a perfect interval, with
            // this voice getting there by leap. Not parallel - the interval
            // before may be anything - and audible for the same reason.
            return haveHistory && together && std::abs (ownStep) > 2
                   && (isPerfectFifth (interval) || isOctaveOrUnison (interval));

        case CounterpointRule::voiceCrossing:
            // Measured against which side this voice started on, so a line
            // written below stays below.
            return haveBoth && (move.ownIsAbove ? move.own < move.other : move.own > move.other);

        case CounterpointRule::dissonanceOnStrong:
            return haveBoth && move.strong && ! isConsonant (interval);

        case CounterpointRule::leaps:
            return move.previousOwn >= 0 && std::abs (move.own - move.previousOwn) > 4;

        case CounterpointRule::repeats:
            return move.previousOwn >= 0 && move.own == move.previousOwn;
    }

    return false;
}

/** How much a broken SOFT rule costs. Leaps scale with their size; everything
    else is a flat charge, because "somewhat parallel" is not a thing.
*/
float costOf (CounterpointRule rule, const Move& move, float weight)
{
    if (rule == CounterpointRule::leaps && move.previousOwn >= 0)
    {
        const auto size = std::abs (move.own - move.previousOwn);
        return weight * (float) std::max (0, size - 4);
    }

    return weight;
}

} // namespace

const char* nameOf (CounterpointRule rule) noexcept
{
    switch (rule)
    {
        case CounterpointRule::parallelFifths: return "parallel-fifths";
        case CounterpointRule::parallelOctaves: return "parallel-octaves";
        case CounterpointRule::directFifths: return "direct-fifths";
        case CounterpointRule::voiceCrossing: return "voice-crossing";
        case CounterpointRule::dissonanceOnStrong: return "dissonance-on-strong";
        case CounterpointRule::leaps: return "leaps";
        case CounterpointRule::repeats: return "repeats";
    }

    return "a rule";
}

const std::vector<CounterpointRule>& relaxationOrder()
{
    // Least musically costly to give up, first. Declared and fixed: relaxing in
    // an order that depended on which rules a score happened to write would
    // make one score's failure mode depend on another's.
    static const std::vector<CounterpointRule> order {
        CounterpointRule::directFifths,    CounterpointRule::dissonanceOnStrong,
        CounterpointRule::voiceCrossing,   CounterpointRule::parallelFifths,
        CounterpointRule::parallelOctaves, CounterpointRule::leaps,
        CounterpointRule::repeats
    };

    return order;
}

CounterpointResult generateCounterpoint (const std::vector<Onset>& onsets,
                                         const std::vector<ChordSpan>& spans,
                                         const std::vector<std::vector<int>>& against,
                                         const CounterpointSpec& spec, int lowPitch, int highPitch,
                                         bool ownIsAbove, int stepsPerBar, const SeedPath& path)
{
    CounterpointResult result;

    if (onsets.empty() || spans.empty() || highPitch < lowPitch)
        return result;

    const auto chordAt = [&spans] (int step) -> const ChordSpan*
    {
        for (const auto& span : spans)
            if (step >= span.startStep && step < span.endStep)
                return &span;

        return spans.empty() ? nullptr : &spans.back();
    };

    /** The other voices' pitch at this onset - the LOWEST of them, because that
        is the one intervals are heard against.
    */
    const auto otherAt = [&against] (std::size_t index) -> int
    {
        auto lowest = -1;

        for (const auto& voice : against)
            if (index < voice.size() && voice[index] >= 0)
                lowest = lowest < 0 ? voice[index] : std::min (lowest, voice[index]);

        return lowest;
    };

    std::vector<Partial> beam { Partial {} };
    auto onsetIndex = 0;

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto& onset = onsets[i];
        ++onsetIndex;

        if (onset.isRest)
            continue;

        const auto* span = chordAt (onset.startStep);

        if (span == nullptr)
            continue;

        auto candidates = scalePitchesBetween (span->localKey, lowPitch, highPitch);

        if (candidates.empty())
            continue;

        const auto other = otherAt (i);
        const auto previousOther = i > 0 ? otherAt (i - 1) : -1;
        const auto strong = onset.strength == MetricStrength::barStart
                            || onset.strength == MetricStrength::strongBeat;

        auto rng = path.child ("onset", onsetIndex).rng();

        std::vector<Partial> next;

        // Hard rules are given up in a declared order, and only as far as it
        // takes to have something to sing. Giving up ALL of them at once would
        // make one impossible bar undo the rules for the whole line.
        std::size_t relaxed = 0;

        for (;;)
        {
            next.clear();

            for (const auto& partial : beam)
            {
                for (const auto pitch : candidates)
                {
                    Move move;
                    move.previousOwn = partial.previous;
                    move.own = pitch;
                    move.previousOther = previousOther;
                    move.other = other;
                    move.strong = strong;
                    move.ownIsAbove = ownIsAbove;

                    auto cost = 0.0f;
                    auto forbidden = false;

                    for (auto r = 0; r < numCounterpointRules; ++r)
                    {
                        const auto rule = (CounterpointRule) r;
                        const auto& setting = spec.rules[(std::size_t) r];

                        if (setting.strength == RuleStrength::off || ! breaks (rule, move))
                            continue;

                        if (setting.strength == RuleStrength::forbid)
                        {
                            // Relaxed rules still cost: a rule given up is not
                            // a rule that stopped mattering.
                            const auto& order = relaxationOrder();
                            const auto position = std::find (order.begin(), order.end(), rule);
                            const auto index = (std::size_t) (position - order.begin());

                            if (index < relaxed)
                            {
                                cost += 8.0f;
                                continue;
                            }

                            forbidden = true;
                            break;
                        }

                        cost += costOf (rule, move, setting.weight);
                    }

                    if (forbidden)
                        continue;

                    // A chord tone is preferred, and the line prefers to move
                    // by step - the two things that make it a line rather than
                    // a sequence of legal intervals.
                    if (! span->chord.containsPitchClass (pitch))
                        cost += strong ? 3.0f : 0.75f;

                    if (partial.previous >= 0)
                    {
                        const auto size = std::abs (pitch - partial.previous);
                        cost += size <= 2 ? 0.0f : (float) (size - 2) * 0.75f;
                    }

                    if (spec.variance > 0.0f)
                        cost += rng.unitFloat() * spec.variance * 12.0f;

                    auto extended = partial;
                    extended.pitches.push_back (pitch);
                    extended.cost += cost;
                    extended.previous = pitch;

                    next.push_back (std::move (extended));
                }
            }

            if (! next.empty())
                break;

            if (relaxed >= relaxationOrder().size())
            {
                // Nothing left to give up. The line stops rather than being
                // filled with something arbitrary, and the caller is told.
                return result;
            }

            result.relaxations.push_back (
                { relaxationOrder()[relaxed], onset.startStep / std::max (1, stepsPerBar) + 1 });
            ++relaxed;
        }

        std::stable_sort (next.begin(), next.end(),
                          [] (const Partial& a, const Partial& b) { return a.cost < b.cost; });

        if ((int) next.size() > beamWidth)
            next.resize ((std::size_t) beamWidth);

        beam = std::move (next);
    }

    if (beam.empty())
        return result;

    const auto& best = beam.front();
    std::size_t taken = 0;

    for (const auto& onset : onsets)
    {
        if (onset.isRest || taken >= best.pitches.size())
            continue;

        auto length = onset.lengthSteps;

        if (spec.articulation == Articulation::detached && length > 1)
            length = std::max (1, length / 2);

        result.notes.push_back ({ onset.startStep, length, best.pitches[taken] });
        ++taken;
    }

    return result;
}

} // namespace dew::lang
