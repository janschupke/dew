#include "lang/Voicing.h"

#include <algorithm>
#include <cstdlib>

namespace dew::lang
{

namespace
{

/** A hard cap on how many layouts are considered.

    A truncation that is SILENT is a determinism hole - the same chord could be
    voiced differently depending on how many candidates happened to be built -
    so the enumeration order is fixed and the cap is far above what any chord in
    range produces.
*/
constexpr int maxCandidates = 512;

bool perfectInterval (int semitones) noexcept
{
    const auto interval = std::abs (semitones) % 12;
    return interval == 0 || interval == 7;
}

/** Stacks the chord's tones upward from a starting inversion, taking `voices`
    of them and wrapping octaves as it runs out.
*/
std::vector<int> stackFrom (const Chord& chord, int inversion, int bassPitch, int voices)
{
    std::vector<int> pitches;

    if (chord.intervals.empty())
        return pitches;

    const auto count = (int) chord.intervals.size();
    auto previous = bassPitch;
    pitches.push_back (bassPitch);

    for (auto v = 1; v < voices; ++v)
    {
        const auto which = (inversion + v) % count;
        const auto octaveOffset = (inversion + v) / count;

        auto pitch = chord.rootPc + chord.intervals[(std::size_t) which] + 12 * octaveOffset;

        // Lift into the octave above the voice below, so the stack always
        // ascends and a doubled tone lands an octave up rather than in unison.
        while (pitch <= previous)
            pitch += 12;

        while (pitch - previous > 24)
            pitch -= 12;

        pitches.push_back (pitch);
        previous = pitch;
    }

    return pitches;
}

/** `drop2` and `drop3` lower the nth voice from the top by an octave. Applied
    as a transform rather than searched for, because that is what they ARE.
*/
void applySpread (std::vector<int>& pitches, Spread spread)
{
    const auto count = (int) pitches.size();

    auto drop = [&] (int fromTop)
    {
        if (count > fromTop)
            pitches[(std::size_t) (count - 1 - fromTop)] -= 12;
    };

    if (spread == Spread::drop2)
        drop (1);
    else if (spread == Spread::drop3)
        drop (2);
    else if (spread == Spread::open && count >= 3)
        drop (1);

    std::sort (pitches.begin(), pitches.end());
}

/** Which chord tones sound, for the shapes that use fewer than all of them. */
Chord reduceForSpread (const Chord& chord, Spread spread)
{
    if (spread != Spread::shell && spread != Spread::rootless)
        return chord;

    Chord reduced = chord;
    reduced.intervals.clear();

    for (std::size_t i = 0; i < chord.intervals.size(); ++i)
    {
        const auto interval = chord.intervals[i];
        const auto degree = interval % 12;

        if (spread == Spread::shell)
        {
            // Root, third and seventh: the notes that say which chord it is.
            if (i == 0 || degree == 3 || degree == 4 || degree == 10 || degree == 11)
                reduced.intervals.push_back (interval);
        }
        else
        {
            // Rootless: everything but the root.
            if (i != 0)
                reduced.intervals.push_back (interval);
        }
    }

    if (reduced.intervals.empty())
        reduced.intervals = chord.intervals;

    return reduced;
}

int rangeCost (const std::vector<int>& pitches, int low, int high)
{
    auto cost = 0;

    for (const auto pitch : pitches)
    {
        if (pitch < low)
            cost += low - pitch;
        else if (pitch > high)
            cost += pitch - high;
    }

    return cost;
}

int spacingCost (const std::vector<int>& pitches)
{
    auto cost = 0;

    // Gaps between the UPPER voices only: a wide gap above the bass is normal
    // and a wide gap in the middle of a chord is not.
    for (std::size_t i = 2; i < pitches.size(); ++i)
        if (const auto gap = pitches[i] - pitches[i - 1]; gap > 12)
            cost += gap - 12;

    return cost;
}

int parallelCost (const std::vector<int>& from, const std::vector<int>& to)
{
    if (from.size() != to.size() || from.size() < 2)
        return 0;

    auto cost = 0;

    for (std::size_t a = 0; a < from.size(); ++a)
        for (std::size_t b = a + 1; b < from.size(); ++b)
        {
            const auto before = from[b] - from[a];
            const auto after = to[b] - to[a];

            if (! perfectInterval (before) || before != after)
                continue;

            // Same perfect interval, and both voices actually moved: parallel
            // fifths or octaves. Holding both still is not parallel motion.
            if (to[a] != from[a] && to[b] != from[b])
                cost += 1;
        }

    return cost;
}

} // namespace

int voiceLeadingCost (const std::vector<int>& from, const std::vector<int>& to)
{
    if (from.empty() || from.size() != to.size())
        return 0;

    auto cost = 0;

    for (std::size_t i = 0; i < from.size(); ++i)
        cost += std::abs (to[i] - from[i]);

    return cost;
}

std::vector<int> voiceChord (const Chord& chord, const VoicingSpec& voicing,
                             const std::vector<int>& previous)
{
    const auto shaped = reduceForSpread (chord, voicing.spread);

    if (shaped.intervals.empty())
        return {};

    const auto voices = std::max (1, voicing.voices);
    const auto inversionCount = (int) shaped.intervals.size();

    std::vector<int> best;
    auto bestCost = 0;
    auto considered = 0;

    // A fixed enumeration order: inversion, then octave, ascending. Ties break
    // to the first candidate, so the result is a pure function of the inputs.
    for (auto inversion = 0; inversion < inversionCount; ++inversion)
    {
        const auto bassPc = pitchClassOf (shaped.rootPc
                                          + shaped.intervals[(std::size_t) inversion]);

        for (auto bass = voicing.lowPitch - 12; bass <= voicing.highPitch; ++bass)
        {
            if (pitchClassOf (bass) != bassPc)
                continue;

            if (considered >= maxCandidates)
                break;

            ++considered;

            auto pitches = stackFrom (shaped, inversion, bass, voices);

            if ((int) pitches.size() != voices)
                continue;

            applySpread (pitches, voicing.spread);

            if (pitches.front() < lowestPitch || pitches.back() > highestPitch)
                continue;

            auto cost = 4 * rangeCost (pitches, voicing.lowPitch, voicing.highPitch)
                      + 2 * spacingCost (pitches);

            if (! previous.empty() && voicing.motion != Motion::fixed)
            {
                const auto motion = voiceLeadingCost (previous, pitches);

                // `smooth` is what the weight is FOR: it makes total motion the
                // dominant term. `parallel` deliberately ignores motion, so a
                // chord shape simply transposes.
                cost += (voicing.motion == Motion::smooth ? 3 : 0) * motion
                      + 6 * parallelCost (previous, pitches);

                if (previous.size() == pitches.size())
                    for (std::size_t i = 0; i < pitches.size(); ++i)
                        if (std::abs (pitches[i] - previous[i]) > voicing.maxLeap)
                            cost += 8;
            }

            if (best.empty() || cost < bestCost)
            {
                best = pitches;
                bestCost = cost;
            }
        }
    }

    // Every candidate fell outside the pitch limits, which a register of one
    // note can do. A chord that sounds somewhere beats one that does not sound.
    if (best.empty())
        best = stackFrom (shaped, 0, std::clamp (voicing.lowPitch, lowestPitch, highestPitch),
                          voices);

    return best;
}

} // namespace dew::lang
