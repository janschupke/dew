#pragma once

#include <juce_core/juce_core.h>

namespace dew::timeText
{

/** Wall-clock time, written the two ways dew has reason to write it.

    Header-only and shared because there were about to be two of these and the
    first one was already private to RenderPanel.cpp. They are deliberately not
    one function: a render's summary is a DURATION, where a tenth of a second
    is information about the file you are producing, and the transport's
    readout is a CLOCK, where a digit that changes ten times a second beside a
    bar count that changes four is noise on the busiest strip in the window.
*/

/** m:ss, zero-padded, for a position being watched.

    Never negative and never blank: this sits beside the bar readout, and a
    field that empties itself is a field that makes the strip jump.
*/
inline juce::String clock (double seconds)
{
    const auto whole = (int) juce::jmax (0.0, seconds);

    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

/** m:ss.s, for a length being reported. A dash when there is nothing to say,
    which is what a render with no material has to show. */
inline juce::String duration (double seconds)
{
    if (seconds <= 0.0)
        return "-";

    const auto minutes = (int) (seconds / 60.0);
    const auto remainder = seconds - (double) minutes * 60.0;

    return juce::String (minutes) + ":" + juce::String (remainder, 1).paddedLeft ('0', 4);
}

} // namespace dew::timeText
