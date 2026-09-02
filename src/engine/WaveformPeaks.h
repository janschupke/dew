#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace dew
{

/** A sample reduced to min/max pairs, for drawing.

    Deliberately not juce::AudioThumbnail. That class owns a cache directory,
    loads asynchronously and hands back a component-shaped drawing call, none of
    which suits a codebase that tests its UI by painting into an image on one
    thread and asserting on the pixels. This is a pure function of a buffer:
    the same audio always produces the same bins, so what a row draws can be
    checked without drawing it.

    Computed once per source when the file is read, and kept beside it in the
    SamplePool - a waveform row repaints far more often than a sample changes.
*/
struct WaveformPeaks
{
    struct Bin
    {
        float minimum = 0.0f;
        float maximum = 0.0f;
    };

    /** Enough detail for a full-width playlist clip on a large display, and
        small enough that a bin is cheaper than the pixels it covers.
    */
    static constexpr int defaultResolution = 2048;

    std::vector<Bin> bins;

    bool isEmpty() const noexcept  { return bins.empty(); }

    /** Reduces every channel of `audio` to `numBins` min/max pairs.

        Channels are folded together by taking the extremes across all of them
        rather than by averaging: a waveform display is about how far the signal
        swings, and averaging a stereo pair that is out of phase would draw
        silence where the file is loud.
    */
    static WaveformPeaks compute (const juce::AudioBuffer<float>& audio,
                                  int numBins = defaultResolution);

    /** The bin covering `position` in 0..1. Clamped, so a rounding error at the
        right-hand edge of a clip cannot index off the end.
    */
    Bin at (float position) const noexcept;

    /** The extremes over [from, to], both in 0..1. What a column of pixels
        spanning more than one bin should draw, rather than one sampled bin -
        picking a single bin per column makes a zoomed-out waveform flicker as
        it scrolls, because which peak gets sampled changes with the offset.
    */
    Bin range (float from, float to) const noexcept;
};

} // namespace dew
