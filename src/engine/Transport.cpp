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

void Transport::setLoopRange (double startSteps, double endSteps) noexcept
{
    loopStartSteps = juce::jmax (0.0, startSteps);
    loopEndSteps   = juce::jmax (0.0, endSteps);
}

juce::int64 Transport::loopStartSamples() const noexcept
{
    return (juce::int64) std::llround (samplesPerStep() * loopStartSteps);
}

juce::int64 Transport::loopEndSamples() const noexcept
{
    return (juce::int64) std::llround (samplesPerStep() * loopEndSteps);
}

juce::int64 Transport::wrappedIntoLoop (juce::int64 position, juce::int64 startSamples,
                                        juce::int64 endSamples) noexcept
{
    // A window narrower than a sample once rounded is not a loop. Bailing out
    // leaves the transport free-running rather than dividing by zero or pinning
    // the playhead to one sample forever.
    const auto loopSamples = endSamples - startSamples;

    if (loopSamples <= 0)
        return position;

    // Before the loop: play INTO it rather than snapping to its start. Setting a
    // loop four bars ahead should not teleport the music there mid-phrase; when
    // the playhead reaches the end the fold below takes over.
    if (position < startSamples)
        return position;

    // Modulo rather than the old subtract-in-a-while. Both operands are
    // non-negative here so it is the same arithmetic, but it is constant time -
    // which starts to matter the moment a loop can be set a thousand bars behind
    // a playhead.
    return startSamples + (position - startSamples) % loopSamples;
}

void Transport::wrapIntoLoop() noexcept
{
    if (! hasLoop())
        return;

    // Each end rounded from the tempo separately, then handed over - NOT
    // llround (samplesPerStep() * (end - start)). At a tempo whose step is not a
    // whole number of samples the two differ by one, and only this one puts the
    // loop's ENDS where the ruler draws them.
    positionSamples = wrappedIntoLoop (positionSamples, loopStartSamples(), loopEndSamples());
}

void Transport::advance (int numSamples) noexcept
{
    positionSamples += numSamples;
    wrapIntoLoop();
}

double Transport::getPositionInSteps() const noexcept
{
    const auto sps = samplesPerStep();
    return sps > 0.0 ? (double) positionSamples / sps : 0.0;
}

} // namespace dew
