#include "engine/Metronome.h"

#include <cmath>

namespace dew
{

void Metronome::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : kDefaultSampleRate;
    clickSamples = juce::jmax (1, (int) (clickSeconds * sampleRate));

    // The one expensive call in the class, and it is here rather than in
    // strike() for that reason. A click falls to a thousandth of its peak over
    // its length, which is silence beside anything else in the mix and leaves
    // no step at the end to hear.
    decay = (float) std::pow (0.001, 1.0 / (double) clickSamples);

    reset();
}

void Metronome::reset() noexcept
{
    remaining = 0;
    phase = 0.0;
    gain = 0.0f;
}

void Metronome::strike (bool accent) noexcept
{
    phase = 0.0;
    phaseDelta = juce::MathConstants<double>::twoPi * (accent ? accentHz : beatHz) / sampleRate;
    gain = accent ? accentGain : beatGain;
    remaining = clickSamples;
}

void Metronome::render (float* left, float* right, int numSamples) noexcept
{
    if (remaining <= 0 || numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    const auto count = juce::jmin (numSamples, remaining);

    for (int i = 0; i < count; ++i)
    {
        const auto sample = gain * (float) std::sin (phase);

        left[i] += sample;
        right[i] += sample;

        phase += phaseDelta;
        gain *= decay;
    }

    remaining -= count;

    // Kept inside the circle rather than allowed to grow: a click is thirty
    // milliseconds, so this can only ever wrap a few thousand times, but the
    // voice is restarted from a phase of zero and a drifting double would make
    // two clicks of the same pitch different waveforms.
    if (phase > juce::MathConstants<double>::twoPi)
        phase -= juce::MathConstants<double>::twoPi
                 * std::floor (phase / juce::MathConstants<double>::twoPi);

    if (remaining <= 0)
        reset();
}

juce::int64 countInSamplesFor (int bars, const Meter& meter, double bpm, double sampleRate) noexcept
{
    const auto samplesPerStep = Transport::samplesPerStepFor (bpm, meter.stepsPerBeat, sampleRate);

    return (juce::int64) std::llround (samplesPerStep * (double) meter.stepsPerBar()
                                       * (double) juce::jmax (1, bars));
}

} // namespace dew
