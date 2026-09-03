#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/WaveformPeaks.h"

using namespace dew;

namespace
{

juce::AudioBuffer<float> ramp (int numSamples, int numChannels = 1)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (channel, i,
                              -1.0f + 2.0f * (float) i / (float) juce::jmax (1, numSamples - 1));

    return buffer;
}

} // namespace

TEST_CASE ("peaks reduce a buffer to the requested number of bins", "[waveform]")
{
    const auto peaks = WaveformPeaks::compute (ramp (4096), 256);
    REQUIRE (peaks.bins.size() == 256);
}

TEST_CASE ("a buffer shorter than the resolution gets one bin per sample", "[waveform]")
{
    // Asking for 2048 bins of a 100-sample recording must not produce 1948 flat
    // ones, which would draw as a line through the middle of the waveform.
    const auto peaks = WaveformPeaks::compute (ramp (100), 2048);

    REQUIRE (peaks.bins.size() == 100);

    for (const auto& bin : peaks.bins)
        REQUIRE (bin.maximum >= bin.minimum);
}

TEST_CASE ("peaks span the whole buffer", "[waveform]")
{
    // A ramp from -1 to +1: the first bin has to reach the bottom and the last
    // the top, or the mapping has dropped samples at one end.
    const auto peaks = WaveformPeaks::compute (ramp (4096), 64);

    REQUIRE (peaks.bins.front().minimum == Catch::Approx (-1.0f).margin (0.01));
    REQUIRE (peaks.bins.back().maximum == Catch::Approx (1.0f).margin (0.01));
}

TEST_CASE ("silence has no extent", "[waveform]")
{
    juce::AudioBuffer<float> silent (1, 1024);
    silent.clear();

    const auto peaks = WaveformPeaks::compute (silent, 32);

    REQUIRE (peaks.bins.size() == 32);

    for (const auto& bin : peaks.bins)
    {
        REQUIRE (bin.minimum == Catch::Approx (0.0f));
        REQUIRE (bin.maximum == Catch::Approx (0.0f));
    }
}

TEST_CASE ("an empty buffer produces no bins rather than a crash", "[waveform]")
{
    const juce::AudioBuffer<float> empty;
    REQUIRE (WaveformPeaks::compute (empty, 128).isEmpty());
}

TEST_CASE ("channels fold by extremes, not by averaging", "[waveform]")
{
    // A pair fully out of phase averages to nothing. A waveform display is
    // about how far the signal swings, so both sides have to survive - the
    // opposite choice from SignalTap, which is a mono listener by design.
    juce::AudioBuffer<float> opposed (2, 512);

    for (int i = 0; i < 512; ++i)
    {
        opposed.setSample (0, i, 0.8f);
        opposed.setSample (1, i, -0.8f);
    }

    const auto peaks = WaveformPeaks::compute (opposed, 16);

    REQUIRE (peaks.bins.front().maximum == Catch::Approx (0.8f));
    REQUIRE (peaks.bins.front().minimum == Catch::Approx (-0.8f));
}

TEST_CASE ("a range covers every bin it spans", "[waveform]")
{
    juce::AudioBuffer<float> spike (1, 1000);
    spike.clear();
    spike.setSample (0, 500, 1.0f);

    const auto peaks = WaveformPeaks::compute (spike, 100);

    // The spike is in one bin, but a column of pixels covering half the buffer
    // must still show it - sampling one bin per column is what makes a
    // zoomed-out waveform shimmer as it scrolls.
    REQUIRE (peaks.range (0.0f, 1.0f).maximum == Catch::Approx (1.0f));
    REQUIRE (peaks.range (0.0f, 0.4f).maximum == Catch::Approx (0.0f));
}

TEST_CASE ("a range out of order and out of bounds is clamped", "[waveform]")
{
    const auto peaks = WaveformPeaks::compute (ramp (2048), 64);

    // Reversed arguments, and past both ends: a rounding error at the edge of a
    // clip must not index off the end of the bins.
    REQUIRE (peaks.range (1.5f, -0.5f).maximum >= peaks.range (1.5f, -0.5f).minimum);
    REQUIRE (peaks.at (-1.0f).maximum <= 1.0f);
    REQUIRE (peaks.at (2.0f).maximum <= 1.0f);
}
