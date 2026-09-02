#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

/** Painting a component offscreen and measuring what came out.

    Every UI test in dew works this way - there is no message loop and no peer,
    so the only way to ask whether something drew is to draw it into an Image and
    count pixels. Four files had a byte-identical copy of `render`, two more had
    the same `coverageOf` and `inkCoverage` again, and a fifth had `render` under
    another name.

    Two measurements are deliberately NOT here: AudioUiTests' `inkFraction`,
    which compares against tokens::colour::background rather than the corner
    pixel, and UiSmokeTests' `fractionOfNonBackgroundPixels`, which samples every
    second pixel. They measure different things from `inkCoverage` and from each
    other, and folding them together would quietly change what those tests assert.
*/
namespace dew::testing
{

inline juce::Image render (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

/** Fraction of pixels close to a given colour. Used to check that a control's
    VALUE is reflected in what it draws, not merely that it drew something.
*/
inline float coverageOf (const juce::Image& image, juce::Colour target)
{
    int matching = 0, sampled = 0;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x, ++sampled)
        {
            const auto pixel = image.getPixelAt (x, y);

            if (std::abs ((int) pixel.getRed()   - (int) target.getRed())   < 24
                && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) < 24
                && std::abs ((int) pixel.getBlue()  - (int) target.getBlue())  < 24
                && pixel.getAlpha() > 200)
                ++matching;
        }
    }

    return sampled > 0 ? (float) matching / (float) sampled : 0.0f;
}

/** Fraction of pixels differing from the top-left one, which every dew surface
    fills with its own background before it draws anything else.
*/
inline float inkCoverage (const juce::Image& image)
{
    const auto background = image.getPixelAt (0, 0);
    int differing = 0, sampled = 0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x, ++sampled)
            if (image.getPixelAt (x, y) != background)
                ++differing;

    return sampled > 0 ? (float) differing / (float) sampled : 0.0f;
}

/** Mean brightness over non-transparent pixels. Text that is bigger, or in a
    lighter grey, raises this; it is how "readable" is measured here.
*/
inline float meanBrightness (const juce::Image& image)
{
    double total = 0.0;
    int counted = 0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (const auto pixel = image.getPixelAt (x, y); pixel.getAlpha() > 0)
            {
                total += pixel.getBrightness();
                ++counted;
            }

    return counted > 0 ? (float) (total / counted) : 0.0f;
}

} // namespace dew::testing
