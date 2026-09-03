#include "model/InstrumentType.h"

namespace dew
{

// These fall back rather than refusing, and deliberately so: they read a
// property that the schema has already defaulted and coerced, so the string
// they are handed is one of these or the file was hand-edited. The one place
// that DOES refuse is instrumentTypeFor, in ModuleCatalog, because `source`
// decides which module renders the channel and guessing there is a project
// that plays back wrong and silently.

Waveform waveformFromString (const juce::String& s)
{
    if (s == "sine")     return Waveform::sine;
    if (s == "square")   return Waveform::square;
    if (s == "triangle") return Waveform::triangle;
    return Waveform::saw;
}

juce::String waveformToString (Waveform w)
{
    switch (w)
    {
        case Waveform::sine:     return "sine";
        case Waveform::square:   return "square";
        case Waveform::triangle: return "triangle";
        case Waveform::saw:      break;
    }

    return "saw";
}

OscMode oscModeFromString (const juce::String& s)
{
    return s == "wavetable" ? OscMode::wavetable : OscMode::classic;
}

juce::String oscModeToString (OscMode m)
{
    switch (m)
    {
        case OscMode::wavetable: return "wavetable";
        case OscMode::classic:   break;
    }

    return "classic";
}

PositionSource positionSourceFromString (const juce::String& s)
{
    return s == "lfo" ? PositionSource::lfo : PositionSource::envelope;
}

juce::String positionSourceToString (PositionSource p)
{
    switch (p)
    {
        case PositionSource::lfo:      return "lfo";
        case PositionSource::envelope: break;
    }

    return "envelope";
}

} // namespace dew
