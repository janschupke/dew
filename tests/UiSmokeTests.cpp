#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MainComponent.h"

namespace
{

/** Paints a component into an offscreen image. This is how the UI gets verified
    in CI and on machines where screen-recording permission is not granted: no
    display, no window server interaction, just the paint path.
*/
juce::Image renderToImage (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

/** Fraction of pixels differing from the top-left pixel. A component that laid
    out and painted has content; one that silently failed is a flat fill.
*/
float fractionOfNonBackgroundPixels (const juce::Image& image)
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

} // namespace

TEST_CASE ("the main component lays out and paints", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;

    REQUIRE (component.getWidth()  > 0);
    REQUIRE (component.getHeight() > 0);

    const auto image = renderToImage (component);
    REQUIRE (image.isValid());

    // Text was drawn: some pixels differ from the flat background fill.
    const auto content = fractionOfNonBackgroundPixels (image);
    INFO ("non-background pixel fraction: " << content);
    REQUIRE (content > 0.0f);
}

TEST_CASE ("the main component survives being resized to its limits", "[ui][smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    dew::MainComponent component;

    for (auto size : { juce::Point<int> { 800, 520 }, juce::Point<int> { 2400, 1400 } })
    {
        component.setSize (size.x, size.y);
        const auto image = renderToImage (component);
        REQUIRE (image.getWidth()  == size.x);
        REQUIRE (image.getHeight() == size.y);
        REQUIRE (fractionOfNonBackgroundPixels (image) > 0.0f);
    }
}
