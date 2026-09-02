#include "engine/WaveformPeaks.h"

namespace dew
{

WaveformPeaks WaveformPeaks::compute (const juce::AudioBuffer<float>& audio, int numBins)
{
    WaveformPeaks peaks;

    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || numBins <= 0)
        return peaks;

    // A buffer shorter than the requested resolution gets one bin per sample
    // rather than empty bins: asking for 2048 bins of a 100-sample recording
    // should not produce 1948 flat ones that draw as a line through the middle.
    const auto binCount = juce::jmin (numBins, numSamples);

    peaks.bins.resize ((size_t) binCount);

    for (int bin = 0; bin < binCount; ++bin)
    {
        // From the bin index rather than by accumulating a step, so the last
        // bin always ends exactly at numSamples however the division rounds.
        const auto begin = (int) ((juce::int64) bin * numSamples / binCount);
        const auto end = juce::jmax (begin + 1,
                                     (int) ((juce::int64) (bin + 1) * numSamples / binCount));

        auto lowest = 0.0f;
        auto highest = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto extremes = juce::FloatVectorOperations::findMinAndMax (
                audio.getReadPointer (channel) + begin, end - begin);

            lowest = juce::jmin (lowest, extremes.getStart());
            highest = juce::jmax (highest, extremes.getEnd());
        }

        peaks.bins[(size_t) bin] = { lowest, highest };
    }

    return peaks;
}

WaveformPeaks::Bin WaveformPeaks::at (float position) const noexcept
{
    if (bins.empty())
        return {};

    const auto index = juce::jlimit (0, (int) bins.size() - 1,
                                     (int) (position * (float) bins.size()));

    return bins[(size_t) index];
}

WaveformPeaks::Bin WaveformPeaks::range (float from, float to) const noexcept
{
    if (bins.empty())
        return {};

    if (to < from)
        std::swap (from, to);

    const auto last = (int) bins.size() - 1;
    const auto first = juce::jlimit (0, last, (int) (from * (float) bins.size()));

    // At least one bin wide: a column narrower than a bin still has to draw
    // something, and an empty range would draw a gap in the middle of a
    // waveform that is zoomed in past one bin per pixel.
    const auto stop = juce::jlimit (first, last, (int) (to * (float) bins.size()));

    Bin combined = bins[(size_t) first];

    for (int i = first + 1; i <= stop; ++i)
    {
        combined.minimum = juce::jmin (combined.minimum, bins[(size_t) i].minimum);
        combined.maximum = juce::jmax (combined.maximum, bins[(size_t) i].maximum);
    }

    return combined;
}

} // namespace dew
