#include "Meter.h"

#include <cstdlib>
#include <limits>

#include "Ids.h"

namespace dew
{

const int Meter::beatUnits[5] = { 1, 2, 4, 8, 16 };

int Meter::clampBeatUnit (int value) noexcept
{
    // Nearest rather than a fallback to 4: a file holding 6 meant something
    // closer to 8 than to a silent reset, and the only values that can reach
    // here are ones no UI can produce.
    //
    // Ties round UP - 6 becomes 8, not 4 - because that is what MIDI does with
    // the same number, and a denominator that disagreed with the exported file
    // would be a second answer to the same question.
    int best = 4;
    int bestDistance = std::numeric_limits<int>::max();

    for (auto unit : beatUnits)
    {
        const auto distance = std::abs (unit - value);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }

    return best;
}

Meter Meter::of (const juce::ValueTree& project) noexcept
{
    Meter meter;

    // Absent takes the default rather than the clamp. The schema materialises
    // every property on load, so a tree reaching here without one was built by
    // hand - a test, or the factory mid-construction - and 4/4 is what it
    // means. Clamping instead would silently make it 1/1 and put a bar line
    // between every step.
    if (project.hasProperty (ids::stepsPerBeat))
        meter.stepsPerBeat = juce::jlimit (1, 16, (int) project[ids::stepsPerBeat]);

    if (project.hasProperty (ids::beatsPerBar))
        meter.beatsPerBar = juce::jlimit (1, maxBeatsPerBar, (int) project[ids::beatsPerBar]);

    if (project.hasProperty (ids::beatUnit))
        meter.beatUnit = clampBeatUnit ((int) project[ids::beatUnit]);

    return meter;
}

juce::String Meter::toString() const
{
    return juce::String (beatsPerBar) + "/" + juce::String (beatUnit);
}

} // namespace dew
