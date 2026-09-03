#include "lang/Melody.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace dew::lang
{

namespace
{

const ChordSpan* chordAt (const std::vector<ChordSpan>& spans, int step)
{
    for (const auto& span : spans)
        if (step >= span.startStep && step < span.endStep)
            return &span;

    return spans.empty() ? nullptr : &spans.back();
}

/** The contour's target pitch at a fraction of the way through the phrase. */
float contourTarget (Contour contour, float through, float low, float high)
{
    const auto span = high - low;

    switch (contour)
    {
        case Contour::arch: return low + span * (1.0f - std::abs (2.0f * through - 1.0f));
        case Contour::rise: return low + span * through;
        case Contour::fall: return high - span * through;
        case Contour::flat: return low + span * 0.5f;
        case Contour::wave:
        {
            // Two humps, so a long phrase has some shape without wandering.
            const auto phase = through * 4.0f;
            const auto fold = std::abs (phase - 2.0f * std::floor (phase / 2.0f) - 1.0f);
            return low + span * (1.0f - fold);
        }
    }

    return low + span * 0.5f;
}

} // namespace

float weightOf (MetricStrength strength) noexcept
{
    switch (strength)
    {
        case MetricStrength::barStart:    return 1.0f;
        case MetricStrength::strongBeat:  return 0.75f;
        case MetricStrength::beat:        return 0.5f;
        case MetricStrength::offbeat:     return 0.25f;
        case MetricStrength::subdivision: return 0.1f;
    }

    return 0.5f;
}

MetricStrength strengthAt (int stepInSection, int stepsPerBar, int stepsPerBeat,
                           int beatsPerBar) noexcept
{
    if (stepsPerBar <= 0 || stepsPerBeat <= 0)
        return MetricStrength::beat;

    const auto inBar = ((stepInSection % stepsPerBar) + stepsPerBar) % stepsPerBar;

    if (inBar == 0)
        return MetricStrength::barStart;

    if (inBar % stepsPerBeat != 0)
    {
        // Halfway through a beat is an offbeat; anything finer is a subdivision.
        const auto intoBeat = inBar % stepsPerBeat;
        return intoBeat * 2 == stepsPerBeat ? MetricStrength::offbeat
                                            : MetricStrength::subdivision;
    }

    // The middle of the bar is the secondary strong beat in any even metre.
    const auto beat = inBar / stepsPerBeat;

    if (beatsPerBar % 2 == 0 && beat == beatsPerBar / 2)
        return MetricStrength::strongBeat;

    // In a triple metre the second beat of a three-group carries the weight.
    if (beatsPerBar % 3 == 0 && beatsPerBar > 3 && beat % 3 == 0)
        return MetricStrength::strongBeat;

    return MetricStrength::beat;
}

std::vector<Onset> tileRhythm (const RhythmSpec& rhythm, int totalSteps, int stepsPerBar,
                               int beatUnit, int stepsPerBeat, bool alignBar)
{
    std::vector<Onset> onsets;

    if (rhythm.steps.empty() || totalSteps <= 0 || stepsPerBar <= 0)
        return onsets;

    const auto beatsPerBar = std::max (1, stepsPerBar / std::max (1, stepsPerBeat));

    auto position = 0;
    std::size_t index = 0;

    while (position < totalSteps)
    {
        // `alignBar` restarts the CYCLE on each bar line - it does not skip to
        // one. A cycle shorter than a bar simply repeats inside it; a cycle that
        // does not divide the bar is cut off at the next bar line and begins
        // again, which is what makes a written pattern land where it was
        // written. Without the reset the cycle runs on and phases against the
        // bar, which is a real effect but never an accident.
        if (alignBar && position % stepsPerBar == 0)
            index = 0;

        const auto& step = rhythm.steps[index];

        // Every step carries its own length now, rests and ties included.
        auto length = std::max (1, stepsFor (step.duration, beatUnit, stepsPerBeat));

        // Truncated at the span's end, and dropped if nothing is left: a note
        // running past its section would retrigger on the pattern's next repeat.
        if (position + length > totalSteps)
            length = totalSteps - position;

        if (length <= 0)
            break;

        if (step.isTie && ! onsets.empty() && ! onsets.back().isRest)
        {
            // A tie holds the previous note longer rather than starting one.
            onsets.back().lengthSteps += length;
            position += length;
        }
        else
        {
            Onset onset;
            onset.startStep = position;
            onset.lengthSteps = length;

            // A tie with nothing to tie to is a rest, not a note from nowhere.
            onset.isRest = step.isRest || step.isTie;
            onset.tiedToPrevious = step.isTie;
            onset.strength = strengthAt (position, stepsPerBar, stepsPerBeat, beatsPerBar);

            onsets.push_back (onset);
            position += length;
        }

        index = (index + 1) % rhythm.steps.size();
    }

    return onsets;
}

void applyMuteBudget (std::vector<Onset>& onsets, int count, int window,
                      const SeedPath& path, int stepsPerBar)
{
    if (count <= 0 || window <= 0 || onsets.empty())
        return;

    for (std::size_t start = 0; start < onsets.size(); start += (std::size_t) window)
    {
        const auto stop = std::min (onsets.size(), start + (std::size_t) window);
        const auto size = (int) (stop - start);

        // Never the whole window: a budget that could silence one entirely is
        // not a budget, it is a deletion.
        const auto toMute = std::min (count, size - 1);

        if (toMute <= 0)
            continue;

        std::vector<std::size_t> order;

        for (auto i = start; i < stop; ++i)
            if (! onsets[i].isRest && i != 0)      // never the first onset of the line
                order.push_back (i);

        if (order.empty())
            continue;

        // Weakest first; ties drawn from the window's OWN stream, so adding a
        // section elsewhere cannot change which note here goes quiet.
        auto rng = path.child ("mute", (int) (start / (std::size_t) window)).rng();

        std::vector<std::uint32_t> tiebreak (order.size());

        for (auto& value : tiebreak)
            value = rng.nextBits();

        std::stable_sort (order.begin(), order.end(),
                          [&] (std::size_t a, std::size_t b)
                          {
                              const auto wa = weightOf (onsets[a].strength);
                              const auto wb = weightOf (onsets[b].strength);

                              if (wa < wb) return true;
                              if (wb < wa) return false;

                              const auto ia = std::distance (order.begin(),
                                                             std::find (order.begin(),
                                                                        order.end(), a));
                              const auto ib = std::distance (order.begin(),
                                                             std::find (order.begin(),
                                                                        order.end(), b));

                              return tiebreak[(std::size_t) ia] < tiebreak[(std::size_t) ib];
                          });

        for (auto i = 0; i < toMute && i < (int) order.size(); ++i)
            onsets[order[(std::size_t) i]].isRest = true;
    }

    (void) stepsPerBar;
}

std::vector<MelodyNote> generateMelody (const std::vector<Onset>& onsets,
                                        const std::vector<ChordSpan>& spans,
                                        const MelodySpec& melody,
                                        int lowPitch, int highPitch,
                                        const SeedPath& path)
{
    std::vector<MelodyNote> notes;

    if (onsets.empty() || spans.empty() || highPitch < lowPitch)
        return notes;

    const auto totalSteps = onsets.back().startStep + onsets.back().lengthSteps;

    auto previousPitch = -1;
    auto previousInterval = 0;
    auto onsetIndex = 0;

    for (const auto& onset : onsets)
    {
        ++onsetIndex;

        if (onset.isRest)
            continue;

        const auto* span = chordAt (spans, onset.startStep);

        if (span == nullptr)
            continue;

        // Scale tones of the LOCAL key, which differs from the home key under a
        // tonicisation - that is what stops a melody fighting a secondary
        // dominant.
        auto candidates = scalePitchesBetween (span->localKey, lowPitch, highPitch);

        if (candidates.empty())
            continue;

        const auto strong = onset.strength == MetricStrength::barStart
                         || onset.strength == MetricStrength::strongBeat;

        if (strong && melody.strong == StrongRule::chordTones)
        {
            std::vector<int> chordTones;

            for (const auto pitch : candidates)
                if (span->chord.containsPitchClass (pitch))
                    chordTones.push_back (pitch);

            // Only if there ARE any: a chord whose tones all fall outside the
            // range would otherwise silence the line.
            if (! chordTones.empty())
                candidates = chordTones;
        }

        const auto through = totalSteps > 0 ? (float) onset.startStep / (float) totalSteps
                                            : 0.0f;
        const auto target = contourTarget (melody.contour, through,
                                           (float) lowPitch, (float) highPitch);

        auto rng = path.child ("onset", onsetIndex).rng();

        auto bestPitch = candidates.front();
        auto bestCost = 0.0f;
        auto first = true;

        for (const auto pitch : candidates)
        {
            auto cost = std::abs ((float) pitch - target);

            if (previousPitch >= 0)
            {
                const auto interval = pitch - previousPitch;
                const auto size = std::abs (interval);

                // Steps are cheap, leaps cost more the wider they get.
                cost += size <= 2 ? 0.0f : (float) (size - 2) * 1.5f;

                // A leap wants a step back the other way after it.
                if (std::abs (previousInterval) > 4)
                {
                    const auto opposite = (previousInterval > 0) != (interval > 0);

                    if (! (opposite && size <= 2))
                        cost += 4.0f;
                }

                if (pitch == previousPitch)
                    cost += 2.0f;
            }

            if (! span->chord.containsPitchClass (pitch))
                cost += strong ? 3.0f : 0.75f;

            // The one knob joining variation to determinism: at 0 this is a
            // pure argmin and the output is identical every compile.
            if (melody.variance > 0.0f)
                cost += rng.unitFloat() * melody.variance * 12.0f;

            if (first || cost < bestCost)
            {
                bestPitch = pitch;
                bestCost = cost;
                first = false;
            }
        }

        if (previousPitch >= 0)
            previousInterval = bestPitch - previousPitch;

        previousPitch = bestPitch;

        auto length = onset.lengthSteps;

        if (melody.articulation == Articulation::detached && length > 1)
            length = std::max (1, length * 2 / 3);

        notes.push_back ({ onset.startStep, length, bestPitch });
    }

    return notes;
}

} // namespace dew::lang
