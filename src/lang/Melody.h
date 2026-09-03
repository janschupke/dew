#pragma once

#include <vector>

#include "lang/Harmony.h"
#include "lang/Model.h"
#include "lang/Rng.h"

namespace dew::lang
{

/** Where a note falls in the bar, as a weight. A mute budget drops the weakest
    onsets first, and a strong-beat rule applies to the top two.
*/
enum class MetricStrength
{
    barStart,
    strongBeat,
    beat,
    offbeat,
    subdivision
};

float weightOf (MetricStrength) noexcept;

MetricStrength strengthAt (int stepInSection, int stepsPerBar, int stepsPerBeat,
                           int beatsPerBar) noexcept;

/** One onset produced by tiling a rhythm across a span. */
struct Onset
{
    int startStep = 0;
    int lengthSteps = 0;
    bool isRest = false;
    bool tiedToPrevious = false;
    MetricStrength strength = MetricStrength::beat;
};

/** Tiles a rhythm across [0, totalSteps).

    `alignBar` restarts the cycle at every bar, which is the default and what
    makes a written pattern land where it was written. Without it the cycle
    runs on and phases against the bar, which is a real effect but never an
    accident.

    A final onset that would overrun the span is TRUNCATED, and one truncated to
    nothing is dropped - a note running past its section would retrigger on the
    next repeat of the pattern.
*/
std::vector<Onset> tileRhythm (const RhythmSpec&, int totalSteps, int stepsPerBar,
                               int beatUnit, int stepsPerBeat, bool alignBar = true);

struct MelodyNote
{
    int startStep = 0;
    int lengthSteps = 0;
    int pitch = 60;
};

/** Generates one line over a section's chords.

    At each onset the candidates are the local scale's tones in range, filtered
    to chord tones on a strong beat when the rule says so, and scored on contour
    distance, step-versus-leap, leap recovery, repetition, and a jitter drawn
    from the scoped RNG.

    `variance` is the single knob joining musical variation to determinism: at
    zero the generator is a pure argmin and the output is identical every
    compile; above zero it perturbs the scores, exploring more without ever
    becoming irreproducible.

    `cadenceDegree` is which tone of the last chord the line has to end on -
    1 the root, 3 the third, 5 the fifth, 7 the seventh, 0 for no rule. It
    arrives already CHOSEN: the caller draws it at whatever scope the score
    declared, so nothing in here has to know about seeds or scopes.
*/
std::vector<MelodyNote> generateMelody (const std::vector<Onset>&,
                                        const std::vector<ChordSpan>&,
                                        const MelodySpec&,
                                        int lowPitch, int highPitch,
                                        const SeedPath&,
                                        int cadenceDegree = 0);

/** Applies a mute budget, in place.

    `k of n`: the onsets are partitioned into consecutive windows of n, and in
    each exactly min(k, n - 1) become rests, chosen by lowest metric strength
    with ties drawn from the window's own RNG stream.

    The n - 1 is deliberate: a window can never fall completely silent. The
    first onset of the span is never muted either, because a line that starts
    with a rest reads as a mistake rather than as a choice.
*/
void applyMuteBudget (std::vector<Onset>&, int count, int window, const SeedPath&,
                      int stepsPerBar);

} // namespace dew::lang
