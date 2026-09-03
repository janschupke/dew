#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace dew
{

struct EngineSnapshot;

/** The step <-> time function of an arrangement.

    Without tempo automation this is one multiply, and it says so: isConstant()
    is not an optimisation but a CORRECTNESS requirement. dew's render tests
    compare samples with juce::exactlyEqual, and
    `(60/bpm * sampleRate / stepsPerBeat) * steps` differs in the last bit from
    `(60/bpm/stepsPerBeat) * steps * sampleRate` at some tempos - so a project
    with no tempo curve has to take the arithmetic it has always taken, not the
    same arithmetic rearranged.

    Sample-rate independent on purpose. A snapshot is built once and may be
    rendered at 44.1k and at 96k, exactly as `tempoBpm` is today; Transport
    supplies the rate and does the ONE rounding to samples.

    Monotone by construction and exactly invertible: forward and inverse index
    the same table and interpolate linearly within the same entry, so
    stepsForSeconds (secondsForSteps (s)) returns s to within a rounding - and
    exactly at whole steps, which is the case that matters, because loop ends,
    clip bounds and material lengths are all whole steps.
*/
class TempoMap
{
public:
    /** No tempo automation: one bpm, and every conversion is a multiply. */
    static TempoMap constant (double bpm, int stepsPerBeat) noexcept;

    /** From the tempo automation already resolved in a snapshot. Falls back to
        the constant form when there is none. */
    static TempoMap build (const EngineSnapshot&, juce::StringArray* warnings);

    bool isConstant() const noexcept { return constantTempo; }

    double secondsForSteps (double steps) const noexcept;
    double stepsForSeconds (double seconds) const noexcept;

    /** The instantaneous rate, for a note's duration and for anything that has
        to know how fast time is passing right here. */
    double secondsPerStepAt (double steps) const noexcept;

    /** Run-length encoded tempo, for MidiExporter's tempo meta events. One entry
        when constant. */
    struct Segment
    {
        double startStep = 0.0;
        double bpm = 128.0;
    };

    juce::Span<const Segment> segments() const noexcept { return { tempoSegments.data(), tempoSegments.size() }; }

    /** How long a map may get before it is refused.

        A cap for the same reason kMaxAutomations is one: a project claiming a
        million bars would otherwise build a table of a million doubles on the
        message thread. Past it the map falls back to constant and says so.
    */
    static constexpr int maxSteps = 1 << 17;

private:
    /** Cumulative seconds at the START of each whole step, prefix-summed once at
        build time.

        Per STEP rather than per sample or per curve segment. A closed form would
        only cover the linear case - a bend makes bpm(s) non-elementary and a
        stepped shape makes it piecewise - so a table is the only representation
        uniform over all three shapes. A step at 128bpm is 117ms, far below
        anything audible as a tempo staircase, and it is the grid the whole of
        dew already expresses time in; a finer one would be a second time axis.
    */
    std::vector<double> stepStartSeconds;
    std::vector<Segment> tempoSegments;

    double constantSecondsPerStep = 0.0;
    bool constantTempo = true;
};

} // namespace dew
