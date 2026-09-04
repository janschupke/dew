#include <catch2/catch_test_macros.hpp>

#include <juce_graphics/juce_graphics.h>

TEST_CASE ("every committed screenshot is a picture of something", "[website]")
{
    /*  What is checkable about a screenshot, and what is not.

        Byte comparison is NOT available and that is a decision rather than an
        omission. dew paints with the system typeface - tokens::type::font asks
        for it by height alone - so the pixels are a function of the macOS
        version and the installed fonts, and even the gallery's HEIGHT moves
        with a font metric because it sizes itself to its laid-out content. A
        cmp would fail on somebody else's laptop for a reason with nothing to do
        with dew, which is exactly what THIRD_PARTY.md's missing generation date
        refuses to do.

        So the shots are regenerated deliberately by scripts/gen-shots.sh, and
        this holds what survives that: each one decodes, is the size it was
        asked for, and is not one flat colour. The last is the one that matters
        - a shot of a component that never got a size, or one painted before its
        document was set, is a perfectly valid PNG of a single colour, and "the
        file exists" passes on it.
    */
    const juce::File directory { DEW_WEBSITE_SHOTS_DIR };
    REQUIRE (directory.isDirectory());

    auto scanned = 0;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*.png"))
    {
        const auto file = entry.getFile();
        ++scanned;

        INFO ("shot: " << file.getFileName());
        INFO ("regenerate: cmake --build --preset release && ./scripts/gen-shots.sh");

        const auto image = juce::ImageFileFormat::loadFrom (file);
        REQUIRE (image.isValid());

        // 1440x900 at --scale 2. The gallery finds its own height, so only the
        // floor is asserted; the width is fixed for all six.
        CHECK (image.getWidth() == 2880);
        CHECK (image.getHeight() >= 1800);

        auto differs = false;
        const auto first = image.getPixelAt (0, 0);

        for (auto y = 0; y < image.getHeight() && ! differs; y += 16)
            for (auto x = 0; x < image.getWidth() && ! differs; x += 16)
                differs = image.getPixelAt (x, y) != first;

        CHECK (differs);
    }

    // The real number rather than "more than one", so the set cannot quietly
    // shrink back: a shot the site references and does not have is a build
    // failure over there, and this is what catches one going missing here.
    INFO ("shots under " << directory.getFullPathName());
    CHECK (scanned == 6);
}
