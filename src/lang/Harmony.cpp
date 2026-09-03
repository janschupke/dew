#include "lang/Harmony.h"

#include <algorithm>
#include <numeric>

namespace dew::lang
{

namespace
{

struct Share
{
    int steps = 0;
    long long remainder = 0;
    std::size_t index = 0;
};

} // namespace

std::vector<ChordSpan> layOutHarmony (const HarmonySpec& harmony,
                                      const Key& songKey,
                                      int totalSteps,
                                      int stepsPerBar,
                                      int beatUnit,
                                      int stepsPerBeat,
                                      DiagnosticBag& diagnostics)
{
    std::vector<ChordSpan> spans;

    if (harmony.chords.empty() || totalSteps <= 0)
        return spans;

    const auto key = harmony.key.value_or (songKey);

    // --- what is fixed, and what is left to share ---------------------------
    std::vector<int> fixed (harmony.chords.size(), 0);
    long long absolute = 0;
    long long weightTotal = 0;

    for (std::size_t i = 0; i < harmony.chords.size(); ++i)
    {
        const auto& chord = harmony.chords[i];

        if (chord.hasWeight)
        {
            weightTotal += chord.weight;
        }
        else if (chord.bars.has_value())
        {
            fixed[i] = *chord.bars * stepsPerBar;
            absolute += fixed[i];
        }
        else if (chord.duration.has_value())
        {
            fixed[i] = stepsFor (*chord.duration, beatUnit, stepsPerBeat);
            absolute += fixed[i];
        }
        else
        {
            // A bare chord with neither a weight nor a length is one share.
            weightTotal += 1;
        }
    }

    if (absolute > totalSteps)
    {
        // Pointed at the entry that crossed the line, not at the whole block:
        // the first few chords are fine and saying otherwise is a wild goose
        // chase.
        long long running = 0;
        auto culprit = harmony.chords.back().range;

        for (std::size_t i = 0; i < harmony.chords.size(); ++i)
        {
            running += fixed[i];

            if (running > totalSteps)
            {
                culprit = harmony.chords[i].range;
                break;
            }
        }

        auto& d = diagnostics.error ("E301", "this harmony is longer than its section",
                                     culprit, "the section is already full here");
        d.notes.push_back ("the section holds " + std::to_string (totalSteps)
                           + " steps; the chords need " + std::to_string (absolute));
        return spans;
    }

    const auto remaining = totalSteps - (int) absolute;

    if (weightTotal == 0 && remaining > 0)
    {
        auto& d = diagnostics.error ("E302", "this harmony does not fill its section",
                                     harmony.range,
                                     std::to_string (remaining) + " steps left over");
        d.notes.push_back ("give a chord `xN` to let it take up the slack, or make "
                           "the section shorter");
        d.helps.push_back ("silence nobody asked for sounds plausible, which is why "
                           "this is an error rather than padding");
        return spans;
    }

    // --- share out the remainder --------------------------------------------
    std::vector<Share> shares;
    long long handedOut = 0;

    for (std::size_t i = 0; i < harmony.chords.size(); ++i)
    {
        const auto& chord = harmony.chords[i];

        if (chord.bars.has_value() || chord.duration.has_value())
            continue;

        const auto weight = chord.hasWeight ? (long long) chord.weight : 1LL;
        const auto exact = (long long) remaining * weight;

        Share share;
        share.index = i;
        share.steps = (int) (exact / weightTotal);
        share.remainder = exact % weightTotal;

        handedOut += share.steps;
        shares.push_back (share);
    }

    // The leftover steps go one each to the largest fractional remainder, ties
    // to the earliest entry. Deterministic, and the total is exact.
    auto leftover = remaining - (int) handedOut;

    if (leftover > 0)
    {
        std::vector<std::size_t> order (shares.size());
        std::iota (order.begin(), order.end(), 0);

        std::stable_sort (order.begin(), order.end(),
                          [&] (std::size_t a, std::size_t b)
                          { return shares[a].remainder > shares[b].remainder; });

        for (std::size_t i = 0; i < order.size() && leftover > 0; ++i, --leftover)
            ++shares[order[i]].steps;
    }

    for (const auto& share : shares)
        fixed[share.index] = share.steps;

    // --- build the spans ----------------------------------------------------
    auto position = 0;

    for (std::size_t i = 0; i < harmony.chords.size(); ++i)
    {
        const auto& chord = harmony.chords[i];

        if (fixed[i] <= 0)
        {
            auto& d = diagnostics.error ("E303", "this chord gets no time at all",
                                         chord.range, "rounds to zero steps");
            d.notes.push_back ("there are " + std::to_string (remaining)
                               + " steps to share between "
                               + std::to_string (weightTotal) + " parts");
            d.helps.push_back ("use coarser weights, or a longer section");
            return {};
        }

        const auto resolved = resolveChord (chord.symbol, key, nullptr);

        if (! resolved.has_value())
            continue;   // already reported where it was written

        ChordSpan span;
        span.startStep = position;
        span.endStep = position + fixed[i];
        span.chord = resolved->chord;
        span.localKey = resolved->localKey;
        span.origin = chord.range;

        spans.push_back (span);
        position = span.endStep;

        // A bar check is an assertion about the running position, and it can
        // only be settled now that the shares are known. This is the single
        // highest-value error catcher in the harmony syntax: it turns "the
        // section length changed and everything shifted" into one message.
        if (chord.barCheckAfter && position % stepsPerBar != 0)
        {
            const auto bar = position / stepsPerBar + 1;
            const auto intoBar = position % stepsPerBar;

            auto& d = diagnostics.error ("E304", "this `|` is not on a bar line",
                                         chord.range, "the bar check is here");
            d.notes.push_back ("the position here is bar " + std::to_string (bar)
                               + ", " + std::to_string (intoBar) + " steps in");
        }
    }

    return spans;
}

} // namespace dew::lang
