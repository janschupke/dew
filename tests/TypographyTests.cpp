#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/DewLookAndFeel.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

using namespace dew;

namespace
{

/** The whole scale, so a test can ask "is this height in the system?" rather
    than restating a number the tokens already own.
*/
bool isOnTheScale (float height)
{
    using namespace tokens;

    for (auto token : { type::caption, type::small, type::body, type::title, type::display })
        if (juce::approximatelyEqual (height, token))
            return true;

    return false;
}

juce::Array<juce::File> sourceFiles()
{
    juce::Array<juce::File> files;

    const juce::File root { DEW_SOURCE_DIR };

    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.cpp;*.h"))
        files.add (entry.getFile());

    return files;
}

juce::Image render (juce::Component& c)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                       juce::jmax (1, c.getHeight()), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    return image;
}

/** Mean brightness over non-transparent pixels. Text that is bigger, or in a
    lighter grey, raises this; it is how "readable" is measured here.
*/
float meanBrightness (const juce::Image& image)
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

/** A component whose only job is to run one paint:: helper, so a helper can be
    measured without dragging a whole panel and its document into the test.
*/
class PaintProbe : public juce::Component
{
public:
    explicit PaintProbe (std::function<void (juce::Graphics&, juce::Rectangle<int>)> p)
        : painter (std::move (p))
    {
        setSize (260, 40);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (tokens::colour::well);
        painter (g, getLocalBounds());
    }

private:
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> painter;
};

} // namespace

TEST_CASE ("every font hook lands on the type scale", "[design][type]")
{
    DewLookAndFeel lnf;

    juce::TextButton button { "M" };
    button.setSize (24, 18);

    juce::Slider slider;
    juce::ComboBox box;

    // 18px was the case that produced 10.8: LookAndFeel_V4 computes
    // jmin (16, height * 0.6) and nothing in the system is 10.8.
    CHECK (isOnTheScale (lnf.getTextButtonFont (button, 18).getHeight()));
    CHECK (isOnTheScale (lnf.getTextButtonFont (button, 34).getHeight()));
    CHECK (isOnTheScale (lnf.getPopupMenuFont().getHeight()));
    CHECK (isOnTheScale (lnf.getSliderPopupFont (slider).getHeight()));
    CHECK (isOnTheScale (lnf.getComboBoxFont (box).getHeight()));
    CHECK (isOnTheScale (lnf.getAlertWindowTitleFont().getHeight()));
    CHECK (isOnTheScale (lnf.getAlertWindowMessageFont().getHeight()));
    CHECK (isOnTheScale (lnf.getAlertWindowFont().getHeight()));
}

TEST_CASE ("a slider's own text box is not left at JUCE's default", "[design][type]")
{
    DewLookAndFeel lnf;
    juce::Slider slider;

    std::unique_ptr<juce::Label> label { lnf.createSliderTextBox (slider) };

    REQUIRE (label != nullptr);

    // JUCE's untouched Label default is 15pt, which is taller than the 15px and
    // 16px text boxes the instrument panel and the mixer hand it.
    CHECK (isOnTheScale (label->getFont().getHeight()));
    CHECK (label->getFont().getHeight() <= (float) tokens::size::controlHeightSm);
}

TEST_CASE ("a tooltip is not drawn in JUCE's hard-coded bold", "[design][type]")
{
    DewLookAndFeel lnf;

    const auto bounds = lnf.getTooltipBounds ("Play from the start",
                                              { 100, 100 }, { 0, 0, 1200, 800 });

    CHECK (bounds.getWidth() > 0);
    CHECK (bounds.getHeight() > 0);
    CHECK (juce::Rectangle<int> (0, 0, 1200, 800).contains (bounds));

    // Long text wraps rather than running off the screen.
    const auto longBounds = lnf.getTooltipBounds (juce::String::repeatedString ("a very long tip ", 20),
                                                  { 100, 100 }, { 0, 0, 1200, 800 });
    CHECK (longBounds.getHeight() > bounds.getHeight());
}

TEST_CASE ("an empty state reads louder than a caption", "[design][type]")
{
    PaintProbe emptyState { [] (juce::Graphics& g, juce::Rectangle<int> b)
                            { paint::emptyState (g, b, "No effects yet - use + to add one"); } };

    // What the effect chain used to do: type::small in textDisabled.
    PaintProbe asItWas { [] (juce::Graphics& g, juce::Rectangle<int> b)
                         {
                             g.setColour (tokens::colour::textDisabled);
                             g.setFont (tokens::type::font (tokens::type::small));
                             g.drawText ("No effects yet - use + to add one", b,
                                         juce::Justification::centred, false);
                         } };

    CHECK (meanBrightness (render (emptyState)) > meanBrightness (render (asItWas)));
}

TEST_CASE ("a section heading is not the same size as a control caption", "[design][type]")
{
    PaintProbe heading { [] (juce::Graphics& g, juce::Rectangle<int> b)
                         { paint::sectionHeading (g, b, "EFFECTS"); } };

    PaintProbe caption { [] (juce::Graphics& g, juce::Rectangle<int> b)
                         { paint::caption (g, b, "EFFECTS"); } };

    // "EFFECTS" is a heading and "CUTOFF" is a caption; drawing both at 10pt is
    // what made a panel's own title smaller than the things inside it.
    CHECK (meanBrightness (render (heading)) > meanBrightness (render (caption)));
}

TEST_CASE ("no source file constructs a font outside the design system", "[design][type]")
{
    const auto files = sourceFiles();

    REQUIRE (files.size() > 20);

    juce::StringArray offenders;

    for (const auto& file : files)
    {
        // Tokens.cpp is where type::font and type::monospaced are defined, so it
        // is the one place a raw FontOptions is the point rather than a leak.
        if (file.getFileName() == "Tokens.cpp")
            continue;

        auto lines = juce::StringArray::fromLines (file.loadFileAsString());

        for (int i = 0; i < lines.size(); ++i)
        {
            const auto& line = lines[i];

            if (line.contains ("FontOptions") || line.contains ("juce::Font ("))
                offenders.add (file.getFileName() + ":" + juce::String (i + 1) + "  " + line.trim());
        }
    }

    INFO ("Fonts must come from tokens::type::font / type::monospaced:\n"
          << offenders.joinIntoString ("\n"));
    CHECK (offenders.isEmpty());
}
