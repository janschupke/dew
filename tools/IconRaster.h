#pragma once

#include <juce_graphics/juce_graphics.h>

namespace dew::shot
{

/** The application icon, rasterised from the SVG the theme generator writes.

    Here rather than in a script because the alternative is a rasteriser in the
    Brewfile for one square, and JUCE already parses SVG.

    The PNG this writes is COMMITTED rather than built: juce_add_gui_app wants
    ICON_BIG at configure time, and the tool that produces it is a target in the
    same project. tests/IconTests.cpp holds the result against the SVG's two
    fills, so a palette change fails rather than shipping the old colours.
*/
inline juce::Result rasteriseIcon (const juce::File& source, const juce::File& destination,
                                   int edge)
{
    if (! source.existsAsFile())
        return juce::Result::fail ("no such file: " + source.getFullPathName());

    const auto drawable = juce::Drawable::createFromSVGFile (source);

    if (drawable == nullptr)
        return juce::Result::fail ("could not read an SVG from " + source.getFullPathName());

    juce::Image image (juce::Image::ARGB, edge, edge, true);

    {
        juce::Graphics g (image);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        drawable->drawWithin (g, juce::Rectangle<float> (0.0f, 0.0f, (float) edge, (float) edge),
                              juce::RectanglePlacement::stretchToFit, 1.0f);
    }

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (destination.createOutputStream());

    if (stream == nullptr)
        return juce::Result::fail ("could not create " + destination.getFullPathName());

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, *stream))
        return juce::Result::fail ("could not encode a PNG");

    return juce::Result::ok();
}

} // namespace dew::shot
