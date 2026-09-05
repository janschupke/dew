#include <catch2/catch_test_macros.hpp>

#include <juce_graphics/juce_graphics.h>

TEST_CASE ("the committed app icon is the SVG it was rasterised from", "[design][icon]")
{
    /*  The icon is generated twice over: gen-theme.mjs writes the SVG from the
        palette, and `dew_shot icon` rasterises it to the PNG that ICON_BIG
        needs at configure time. npm run theme:check holds the first link. This
        holds the second, which nothing else can see - a palette change would
        otherwise leave a correct SVG beside a PNG of the old colours, and the
        application would ship the stale one.

        Bytes are not compared, for the reason WebsiteShotTests gives about the
        screenshots: what is asserted is that the two flat fills in the SVG are
        the two colours actually in the PNG, at points that are inside them.
    */
    const juce::File repository { DEW_REPO_DIR };
    const auto svg = repository.getChildFile ("resources/icon/dew.svg");
    const auto png = repository.getChildFile ("resources/icon/dew.png");

    INFO ("regenerate: cmake --build --preset release --target dew_shot && "
          "build/release/tools/dew_shot_artefacts/RelWithDebInfo/dew_shot "
          "icon resources/icon/dew.png resources/icon/dew.svg");

    REQUIRE (svg.existsAsFile());
    REQUIRE (png.existsAsFile());

    // The two fills, in the order the generator writes them: the window colour
    // behind, the accent drop in front.
    juce::StringArray fills;

    for (const auto& line : juce::StringArray::fromLines (svg.loadFileAsString()))
        if (const auto at = line.indexOf ("fill=\"#"); at >= 0)
            fills.add (line.substring (at + 6, at + 13));

    REQUIRE (fills.size() == 2);

    const auto background = juce::Colour::fromString ("ff" + fills[0].substring (1));
    const auto accent = juce::Colour::fromString ("ff" + fills[1].substring (1));
    REQUIRE (background != accent);

    const auto image = juce::ImageFileFormat::loadFrom (png);
    REQUIRE (image.isValid());

    // 1024 square is what juce_add_gui_app wants to derive an .icns and an .ico
    // from; anything smaller is upscaled into the largest slot.
    REQUIRE (image.getWidth() == 1024);
    REQUIRE (image.getHeight() == 1024);

    // The viewBox is 32 units, so a unit is 32px. (4,4) is well inside the
    // rounded corner's arc, and (16,18.4) is the centre of the drop's circle.
    const auto unit = image.getWidth() / 32;

    CHECK (image.getPixelAt (4 * unit, 4 * unit).toDisplayString (false)
           == background.toDisplayString (false));
    CHECK (image.getPixelAt (16 * unit, (int) (18.4f * (float) unit)).toDisplayString (false)
           == accent.toDisplayString (false));
}
