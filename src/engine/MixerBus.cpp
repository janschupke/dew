#include "MixerBus.h"

#include <cmath>

namespace dew
{

bool MixerBus::isAudible (const EngineSnapshot& snapshot, const MixerTrackSnapshot& track) noexcept
{
    if (track.mute)
        return false;

    // Solo anywhere in the mixer means only soloed tracks are heard.
    if (snapshot.anySolo)
        return track.solo;

    return true;
}

void MixerBus::panGains (float pan, float& leftGain, float& rightGain) noexcept
{
    const auto clamped = juce::jlimit (-1.0f, 1.0f, pan);
    const auto angle = (clamped + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    leftGain  = std::cos (angle);
    rightGain = std::sin (angle);
}

MixerBus::TrackGains MixerBus::trackGains (float pan, float gain) noexcept
{
    float leftGain = 0.0f, rightGain = 0.0f;
    panGains (pan, leftGain, rightGain);

    // Written exactly as the three call sites wrote it. Float multiplication is
    // not associative, so re-grouping any of these three would move the last
    // bits of every render and EngineTests' save/load identity would fail.
    return { gain * leftGain * juce::MathConstants<float>::sqrt2,
             gain * rightGain * juce::MathConstants<float>::sqrt2,
             gain * juce::jmax (leftGain, rightGain) * juce::MathConstants<float>::sqrt2 };
}

void MixerBus::addPanned (const float* mono, int numSamples, float gain, float pan,
                          float* left, float* right) noexcept
{
    float leftGain = 0.0f, rightGain = 0.0f;
    panGains (pan, leftGain, rightGain);

    leftGain *= gain;
    rightGain *= gain;

    juce::FloatVectorOperations::addWithMultiply (left, mono, leftGain, numSamples);
    juce::FloatVectorOperations::addWithMultiply (right, mono, rightGain, numSamples);
}

} // namespace dew
