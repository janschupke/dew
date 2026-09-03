#include "model/EffectType.h"

namespace dew
{

FilterMode filterModeFromString (const juce::String& s)
{
    if (s == "highpass")
        return FilterMode::highpass;
    if (s == "bandpass")
        return FilterMode::bandpass;

    return FilterMode::lowpass;
}

juce::String filterModeToString (FilterMode mode)
{
    switch (mode)
    {
        case FilterMode::highpass: return "highpass";
        case FilterMode::bandpass: return "bandpass";
        case FilterMode::lowpass: break;
    }

    return "lowpass";
}

} // namespace dew
