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

std::vector<ChordSpan> layOutHarmony (const HarmonySpec& harmony, const Key& songKey,
                                      int totalSteps, int stepsPerBar, int beatUnit,
                                      int stepsPerBeat, DiagnosticBag& diagnostics)
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

        auto& d = diagnostics.error (
            "E301", diagnostics.text (Msg::harmony_longerThanSection_message), culprit,
            diagnostics.text (Msg::harmony_longerThanSection_label));
        d.notes.push_back (diagnostics.text (
            Msg::harmony_longerThanSection_note,
            MsgArgs {}.with ("held", totalSteps).with ("needed", (std::int64_t) absolute)));
        return spans;
    }

    const auto remaining = totalSteps - (int) absolute;

    if (weightTotal == 0 && remaining > 0)
    {
        auto& d = diagnostics.error (
            "E302", diagnostics.text (Msg::harmony_doesNotFill_message), harmony.range,
            diagnostics.text (Msg::harmony_doesNotFill_label, MsgArgs {}.count (remaining)));
        d.notes.push_back (diagnostics.text (Msg::harmony_doesNotFill_note));
        d.helps.push_back (diagnostics.text (Msg::harmony_doesNotFill_help));
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

        std::stable_sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b)
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
            auto& d = diagnostics.error (
                "E303", diagnostics.text (Msg::harmony_noTimeAtAll_message), chord.range,
                diagnostics.text (Msg::harmony_noTimeAtAll_label));
            d.notes.push_back (
                diagnostics.text (Msg::harmony_noTimeAtAll_note,
                                  MsgArgs {}.count (remaining).with ("parts", weightTotal)));
            d.helps.push_back (diagnostics.text (Msg::harmony_noTimeAtAll_help));
            return {};
        }

        const auto resolved = resolveChord (chord.symbol, key, nullptr);

        if (! resolved.has_value())
            continue; // already reported where it was written

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

            auto& d = diagnostics.error (
                "E304", diagnostics.text (Msg::harmony_barCheckOffLine_message), chord.range,
                diagnostics.text (Msg::harmony_barCheckOffLine_label));
            d.notes.push_back (diagnostics.text (Msg::harmony_barCheckOffLine_note,
                                                 MsgArgs {}.with ("bar", bar).count (intoBar)));
        }
    }

    return spans;
}

} // namespace dew::lang
