#include "engine/SamplePlayer.h"

#include <cmath>

namespace dew
{

namespace
{

/** One source frame, downmixed, with linear interpolation between neighbours.

    Downmixed by averaging rather than by taking channel 0: a stereo recording
    played on a channel that then gets panned should keep both sides, and
    dropping one would quietly halve a hard-panned take.

    The mono fold is the same thing MixerBus does later in the block, so a
    stereo file and a mono file of the same take sound the same here.
*/
float sampleAt (const juce::AudioBuffer<float>& audio, double frame) noexcept
{
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return 0.0f;

    const auto first = (int) std::floor (frame);

    if (first < 0 || first >= numSamples)
        return 0.0f;

    const auto second = juce::jmin (first + 1, numSamples - 1);
    const auto blend = (float) (frame - (double) first);

    auto total = 0.0f;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* data = audio.getReadPointer (channel);
        total += data[first] + (data[second] - data[first]) * blend;
    }

    return total / (float) numChannels;
}

/** Fade gain at `offset` frames into a region `length` frames long. */
float fadeGainAt (const SampleSettings& settings, double offset, int length) noexcept
{
    auto gain = 1.0f;

    if (settings.fadeInSamples > 0 && offset < (double) settings.fadeInSamples)
        gain *= (float) (offset / (double) settings.fadeInSamples);

    if (settings.fadeOutSamples > 0)
    {
        const auto fadeStart = (double) (length - settings.fadeOutSamples);

        if (offset > fadeStart)
            gain *= (float) juce::jmax (0.0, (length - offset) / (double) settings.fadeOutSamples);
    }

    return juce::jlimit (0.0f, 1.0f, gain);
}

} // namespace

double SamplePlayer::readOffsetFor (const SampleSettings& settings, double elapsedOutputSamples,
                                    double engineSampleRate) noexcept
{
    // Two ratios, not one. The transpose is what the user asked for; the rate
    // conversion is what the file needs to play at its own speed on this
    // device. Folding them together on the message thread would have made the
    // stored pitch depend on whatever device was open when it was saved.
    const auto rate = (double) settings.pitchRatio
                    * (engineSampleRate > 0.0 ? settings.sourceSampleRate / engineSampleRate : 1.0);

    return elapsedOutputSamples * rate;
}

void SamplePlayer::renderAdd (float* mono, int numSamples,
                              const EngineSnapshot& snapshot,
                              int channelIndex,
                              double positionSteps,
                              double samplesPerStep,
                              double engineSampleRate) noexcept
{
    if (mono == nullptr || numSamples <= 0 || samplesPerStep <= 0.0)
        return;

    if (channelIndex < 0 || channelIndex >= (int) snapshot.channels.size())
        return;

    const auto& channel = snapshot.channels[(size_t) channelIndex];

    if (channel.source != ChannelSource::audio || channel.audio == nullptr)
        return;

    const auto& audio = *channel.audio;
    const auto& settings = channel.sample;

    const auto region = settings.endSample - settings.startSample;

    if (region <= 0)
        return;

    const auto stepsPerBar = (double) snapshot.stepsPerBar();
    const auto blockSteps = (double) numSamples / samplesPerStep;

    for (const auto& clip : snapshot.clips)
    {
        if (clip.channelIndex != channelIndex || ! clip.trackAudible)
            continue;

        const auto clipStartSteps = (double) clip.startBar * stepsPerBar;
        const auto clipEndSteps = clipStartSteps + (double) clip.lengthBars * stepsPerBar;

        // Half-open on both sides, so a clip ending where the next begins does
        // not render one block of both.
        if (positionSteps + blockSteps <= clipStartSteps || positionSteps >= clipEndSteps)
            continue;

        // Where in this block the clip starts and stops sounding. A clip that
        // began before the block starts at output sample 0, which is what makes
        // seeking into the middle of one work without a special case.
        const auto firstOffset = positionSteps >= clipStartSteps
                                     ? 0
                                     : (int) std::ceil ((clipStartSteps - positionSteps) * samplesPerStep);

        const auto lastOffset = positionSteps + blockSteps <= clipEndSteps
                                    ? numSamples
                                    : (int) std::ceil ((clipEndSteps - positionSteps) * samplesPerStep);

        const auto from = juce::jlimit (0, numSamples, firstOffset);
        const auto to = juce::jlimit (from, numSamples, lastOffset);

        // Output samples since the clip began, which may be negative inside the
        // block only when the clip starts later than it - hence `from`.
        const auto elapsedAtFrom = (positionSteps - clipStartSteps) * samplesPerStep + (double) from;

        auto offset = readOffsetFor (settings, juce::jmax (0.0, elapsedAtFrom), engineSampleRate);
        const auto step = readOffsetFor (settings, 1.0, engineSampleRate);

        for (int i = from; i < to; ++i, offset += step)
        {
            auto position = offset;

            if (settings.loop)
            {
                position = std::fmod (position, (double) region);

                if (position < 0.0)
                    position += (double) region;
            }
            else if (position >= (double) region)
            {
                // Past the end of a one-shot: nothing further in this clip can
                // sound, so stop rather than running the rest of the block.
                break;
            }

            const auto gain = fadeGainAt (settings, position, region);

            const auto frame = settings.reverse
                                   ? (double) (settings.endSample - 1) - position
                                   : (double) settings.startSample + position;

            mono[i] += sampleAt (audio, frame) * gain;
        }
    }
}

} // namespace dew
