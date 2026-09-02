#include "Transport.h"

namespace dew
{

double Transport::samplesPerStepFor (double bpm, int stepsPerBeat, double sampleRate) noexcept
{
    const auto safeBpm = juce::jlimit (1.0, 999.0, bpm);
    const auto safeSteps = juce::jmax (1, stepsPerBeat);

    return (60.0 / safeBpm) * sampleRate / (double) safeSteps;
}

void Transport::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    positionSamples = 0;
}

void Transport::setTempo (double bpm, int newStepsPerBeat)
{
    tempoBpm = juce::jlimit (1.0, 999.0, bpm);
    stepsPerBeat = juce::jmax (1, newStepsPerBeat);
}

double Transport::samplesPerStep() const noexcept
{
    return samplesPerStepFor (tempoBpm, stepsPerBeat, sampleRate);
}

void Transport::advance (int numSamples) noexcept
{
    positionSamples += numSamples;

    if (loopLengthSteps > 0)
    {
        const auto loopSamples = (juce::int64) std::llround (samplesPerStep() * (double) loopLengthSteps);

        if (loopSamples > 0)
            while (positionSamples >= loopSamples)
                positionSamples -= loopSamples;
    }
}

double Transport::getPositionInSteps() const noexcept
{
    const auto sps = samplesPerStep();
    return sps > 0.0 ? (double) positionSamples / sps : 0.0;
}

} // namespace dew
