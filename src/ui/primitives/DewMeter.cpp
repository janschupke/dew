#include "ui/primitives/DewMeter.h"

#include <cmath>

#include "ui/design/Tokens.h"

namespace dew::meter
{

float fall (float current, float incoming, int deltaMs) noexcept
{
    if (incoming >= current)
        return incoming;

    const auto tau = (float) tokens::motion::meterReleaseMs;
    const auto decay = std::exp (-(float) juce::jmax (0, deltaMs) / tau);
    const auto level = incoming + (current - incoming) * decay;

    // Snap to silence rather than approaching it forever, so an idle meter is
    // reachable - a test that waits for zero would otherwise never finish.
    return level < 0.001f ? 0.0f : level;
}

float proportionForGain (float gain) noexcept
{
    if (gain <= 0.0f)
        return 0.0f;

    const auto db = juce::Decibels::gainToDecibels (gain, (float) floorDb);

    return juce::jlimit (0.0f, 1.0f, (float) ((db - floorDb) / -floorDb));
}

} // namespace dew::meter
