#include "model/ParamSpec.h"

#include <cmath>

namespace dew
{

juce::var ParamSpec::defaultVar() const
{
    if (defaultText != nullptr)
        return { defaultText };

    if (control == ParamControl::toggle)
        return { defaultValue != 0.0 };

    if (integral)
        return { (int) defaultValue };

    return { defaultValue };
}

float ParamSpec::clamp (float value) const noexcept
{
    return juce::jlimit ((float) minimum, (float) maximum, value);
}

double ParamSpec::clamp (double value) const noexcept
{
    return juce::jlimit (minimum, maximum, value);
}

double ParamSpec::fromNormalised (double normalised) const noexcept
{
    const auto t = juce::jlimit (0.0, 1.0, normalised);

    // Exactly the endpoints at 0 and 1. A pow() that lands a hair under the
    // maximum would make "all the way up" not quite all the way up, and the
    // difference is audible on a resonant filter.
    if (t <= 0.0)
        return minimum;

    if (t >= 1.0)
        return maximum;

    if (curve == ParamCurve::logarithmic && minimum > 0.0 && maximum > minimum)
        return minimum * std::pow (maximum / minimum, t);

    if (curve == ParamCurve::cubic)
        return minimum + t * t * t * (maximum - minimum);

    return minimum + t * (maximum - minimum);
}

double ParamSpec::toNormalised (double value) const noexcept
{
    const auto clamped = clamp (value);

    if (curve == ParamCurve::logarithmic && minimum > 0.0 && maximum > minimum)
        return std::log (clamped / minimum) / std::log (maximum / minimum);

    if (maximum <= minimum)
        return 0.0;

    const auto linear = (clamped - minimum) / (maximum - minimum);

    if (curve == ParamCurve::cubic)
        return std::cbrt (linear);

    return linear;
}

int ParamSpec::numDiscreteValues() const noexcept
{
    if (control == ParamControl::toggle)
        return 2;

    if (control == ParamControl::choice)
        return juce::jmax (0, numChoices);

    // An integral parameter with a unit step: octave, unison voices. A
    // non-integral one is continuous however coarse its editing interval is -
    // an interval is how far one nudge goes, not what values exist.
    if (integral && juce::approximatelyEqual (interval, 1.0) && maximum > minimum)
        return (int) std::llround (maximum - minimum) + 1;

    return 0;
}

} // namespace dew
