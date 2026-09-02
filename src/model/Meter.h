#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** A project's meter: how steps are grouped into beats and beats into bars.

    Presentation and counting only. `stepsPerBeat` owns how long a step is - it
    is what Transport::samplesPerStepFor divides by - and nothing here touches
    it, so changing the meter regroups the grid without changing playback speed
    or the duration of a single note. That is the whole point of the type: bars
    used to be `stepsPerBeat * 4` written out by hand in fifteen places, and the
    4 had no name.

    `beatUnit` is the notational denominator and is deliberately inert in the
    engine. It names the meter, labels the snap divisions, and is written to the
    MIDI time signature; it is not a tempo. A beat is a beat whatever it is
    called.
*/
struct Meter
{
    int stepsPerBeat = 4;
    int beatsPerBar = 4;

    /** The denominator: 1, 2, 4, 8 or 16. A note value, so a power of two -
        which is also all MIDI can encode, since it stores the base-2 logarithm.
    */
    int beatUnit = 4;

    int stepsPerBar() const noexcept { return stepsPerBeat * beatsPerBar; }

    /** Read from a PROJECT tree and clamped. Never returns a zero in any field,
        because every caller divides or takes a modulo by one of them.
    */
    static Meter of (const juce::ValueTree& project) noexcept;

    /** "4/4". The numerator counts beats; the denominator names them. */
    juce::String toString() const;

    /** The denominators offered, smallest note value last. */
    static const int beatUnits[5];

    /** The nearest legal denominator to `value`, for a tree written by hand or
        by something that did not know the rule.
    */
    static int clampBeatUnit (int value) noexcept;

    static constexpr int maxBeatsPerBar = 16;

    bool operator== (const Meter& other) const noexcept
    {
        return stepsPerBeat == other.stepsPerBeat
            && beatsPerBar == other.beatsPerBar
            && beatUnit == other.beatUnit;
    }

    bool operator!= (const Meter& other) const noexcept { return ! operator== (other); }
};

} // namespace dew
