#pragma once

#include <juce_core/juce_core.h>

namespace dew::pianoRoll
{

/** Twelve, once. It was already spelled four times in PianoRollComponent.cpp -
    two of them inside a `% 12` that is a pitch class and two a transpose limit.
*/
inline constexpr int semitonesPerOctave = 12;

/** Which pitches are the narrow dark keys.

    Header-only and shared rather than file-local, because the keyboard is drawn
    in one translation unit and hit-tested in another, and a piano that disagreed
    with itself about where the black keys are would be a hard bug to see.
*/
inline bool isBlackKey (int pitch)
{
    switch (((pitch % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10: return true;
        default: return false;
    }
}

inline juce::String noteName (int pitch)
{
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    return juce::String (
               names[((pitch % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave])
           + juce::String (pitch / semitonesPerOctave - 1);
}

} // namespace dew::pianoRoll
