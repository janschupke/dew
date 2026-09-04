#pragma once

#include <initializer_list>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "ui/design/Tokens.h"

/** What the three oscillator test files share.

    peakOf and firstChannel are wanted by the engine tests and the schema tests;
    renderToImage and fractionOfNonBackgroundPixels by the panel's tests, which
    check a face is actually DRAWN rather than merely laid out.
*/
namespace dew::testing
{

using namespace dew;

inline float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

/** The first channel of a demo project, which is the one the render tests
    below switch oscillators on and off for.
*/
inline juce::ValueTree firstChannel (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::CHANNEL);
}

inline juce::Image renderToImage (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

inline float fractionOfNonBackgroundPixels (const juce::Image& image)
{
    const auto background = image.getPixelAt (0, 0);
    int differing = 0;

    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y) != background)
                ++differing;

    const auto sampled = (image.getWidth() / 2) * (image.getHeight() / 2);
    return sampled > 0 ? (float) differing / (float) sampled : 0.0f;
}

/** A channel's oscillator slots, whatever built the tree. */
inline void requireThreeSlotsWithOnlyTheFirstOn (const juce::ValueTree& channel)
{
    REQUIRE (ProjectEdits::countOscillators (channel) == kMaxOscillators);
    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 0)[ids::enabled] == true);

    for (int i = 1; i < kMaxOscillators; ++i)
    {
        INFO ("slot " << i);
        REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, i)[ids::enabled] == false);
    }
}

} // namespace dew::testing
