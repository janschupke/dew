#include "engine/RenderPost.h"

namespace dew::RenderPost
{

void applyFades (juce::AudioBuffer<float>& buffer, double sampleRate, double fadeInSeconds,
                 double fadeOutSeconds) noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || sampleRate <= 0.0)
        return;

    // Half the buffer each, so two long fades meet in the middle rather than
    // overlapping into a dip.
    const auto longest = numSamples / 2;

    const auto fadeIn = juce::jlimit (0, longest, (int) std::llround (fadeInSeconds * sampleRate));
    const auto fadeOut = juce::jlimit (0, longest,
                                       (int) std::llround (fadeOutSeconds * sampleRate));

    if (fadeIn > 0)
        buffer.applyGainRamp (0, fadeIn, 0.0f, 1.0f);

    if (fadeOut > 0)
        buffer.applyGainRamp (numSamples - fadeOut, fadeOut, 1.0f, 0.0f);
}

float normalize (juce::AudioBuffer<float>& buffer, float targetPeak, float maxBoostDb) noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return 0.0f;

    const auto peak = buffer.getMagnitude (0, numSamples);

    // Nothing to scale, and nothing sensible to scale it by.
    if (peak <= 1.0e-9f || targetPeak <= 0.0f)
        return 0.0f;

    auto gain = targetPeak / peak;

    const auto maxGain = juce::Decibels::decibelsToGain (maxBoostDb);

    if (gain > maxGain)
        gain = maxGain;

    buffer.applyGain (gain);

    return juce::Decibels::gainToDecibels (gain);
}

void dither (juce::AudioBuffer<float>& buffer, int bitDepth, juce::int64 seed) noexcept
{
    if (bitDepth <= 0 || bitDepth > maxDitheredBitDepth)
        return;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    // The full-scale range is [-1, 1), divided into 2^bitDepth steps.
    const auto lsb = 2.0f / (float) (1 << bitDepth);

    // Just inside full scale: dither must not push a sample that normalize put
    // at exactly 1.0 over the writer's clip threshold, which would turn the
    // loudest sample in the file into a click.
    const auto ceiling = 1.0f - lsb;

    juce::Random random (seed);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            // Two independent uniforms summed give a triangular distribution
            // over +/-1 LSB - the standard choice, because it decorrelates the
            // truncation error without the noise modulation a rectangular
            // distribution leaves behind.
            const auto noise = lsb * (random.nextFloat() - random.nextFloat());

            data[i] = juce::jlimit (-ceiling, ceiling, data[i] + noise);
        }
    }
}

} // namespace dew::RenderPost
