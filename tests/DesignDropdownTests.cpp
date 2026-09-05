// The dropdown: where its menu opens, and how.
//
// Split out of DesignSystemTests.cpp along the tags it already carried. It had
// no shared fixture to divide - its anonymous namespace was empty.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "ui/design/DewGallery.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Icons.h"
#include "ui/design/SignalScope.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"
#include "ui/primitives/HoverTracker.h"

#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("a combo box is painted in the dew idiom, not JUCE's", "[design][dropdown]")
{
    // There were no drawComboBox / drawPopupMenu overrides at all, so a
    // dropdown was a stock widget sitting beside hand-painted primitives.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    juce::ComboBox box;
    box.setLookAndFeel (&lookAndFeel);
    box.addItemList ({ "Sine", "Saw", "Square" }, 1);
    box.setSelectedId (2, juce::dontSendNotification);
    box.setSize (160, tokens::size::controlHeight);

    const auto image = render (box);

    // The control's own surface, rather than whatever JUCE would have used.
    INFO ("surfaceRaised coverage: " << coverageOf (image, tokens::colour::surfaceRaised));
    REQUIRE (coverageOf (image, tokens::colour::surfaceRaised) > 0.5f);

    // The chevron lives in the right-hand end; there has to be ink there.
    const auto chevronStrip = image.getClippedImage (
        { image.getWidth() - 30, 0, 28, image.getHeight() });
    INFO ("chevron ink: " << inkCoverage (chevronStrip));
    REQUIRE (inkCoverage (chevronStrip) > 0.02f);

    box.setLookAndFeel (nullptr);
}

TEST_CASE ("a category heading reads as a heading, not as a row", "[design][dropdown]")
{
    // What the preset pickers needed once their descriptions moved to a
    // tooltip. The second line this class used to pack into a row's text is
    // gone with them: it was a sentinel inside the item text, which is exactly
    // what MenuGlyph.h says a row must not carry.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    auto rowWidth = 0;
    auto rowHeight = 0;
    lookAndFeel.getIdealPopupMenuItemSize ("Hollow Keys", false, 0, rowWidth, rowHeight);

    auto headingWidth = 0;
    auto headingHeight = 0;
    lookAndFeel.getIdealPopupMenuSectionHeaderSizeWithOptions ("Keys", 0, headingWidth,
                                                               headingHeight, {});

    // Shorter than a row, not JUCE's row-and-a-half: a heading a row and a half
    // tall separates the group it is meant to gather.
    INFO ("row " << rowWidth << "x" << rowHeight << ", heading " << headingWidth << "x"
                 << headingHeight);
    CHECK (headingHeight > 0);
    CHECK (headingHeight < rowHeight);

    // And it paints. Drawn straight through the look and feel rather than
    // through a menu window, which a headless test has no peer for.
    struct Heading : juce::Component
    {
        Heading (DewLookAndFeel& l, juce::String t)
            : lookAndFeel (l)
            , text (std::move (t))
        {
        }

        DewLookAndFeel& lookAndFeel;
        juce::String text;

        void paint (juce::Graphics& g) override
        {
            lookAndFeel.drawPopupMenuSectionHeader (g, getLocalBounds(), text);
        }
    };

    Heading named { lookAndFeel, "Keys" };
    named.setSize (rowWidth, headingHeight);

    Heading blank { lookAndFeel, {} };
    blank.setSize (rowWidth, headingHeight);

    const auto namedInk = inkCoverage (render (named));
    const auto blankInk = inkCoverage (render (blank));

    INFO ("ink: named " << namedInk << ", blank " << blankInk);
    CHECK (namedInk > 0.0f);

    // The control case: nothing paints a heading with no name, so the ink above
    // is the text rather than a background this happens to fill. exactlyEqual
    // because the ci preset builds -Wfloat-equal and Catch2 decomposes ==.
    CHECK (juce::exactlyEqual (blankInk, 0.0f));
}

TEST_CASE ("a dropdown's menu opens below the box, not over it", "[design][dropdown]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    juce::ComboBox box;
    box.setLookAndFeel (&lookAndFeel);
    box.addItemList ({ "Sine", "Saw", "Square", "Triangle" }, 1);
    box.setSize (160, tokens::size::controlHeight);

    juce::Label label;
    label.setSize (160, tokens::size::controlHeight);

    // The menu's placement is decided entirely by these options - ComboBox::
    // showPopup passes them straight to PopupMenu - so pinning the contract
    // pins the behaviour. Asserting the rendered position would need a real
    // desktop window, which a headless test does not have.
    for (int selected : { 1, 3, 4 })
    {
        box.setSelectedId (selected, juce::dontSendNotification);

        const auto options = lookAndFeel.getOptionsForComboBoxPopupMenu (box, label);

        // THE bug: V2 sets this, and PopupMenu then drags the window up until
        // the ticked row sits on the box.
        INFO ("selected id " << selected);
        CHECK (options.getItemThatMustBeVisible() == 0);

        // Keyboard navigation still starts from the current value.
        CHECK (options.getInitiallySelectedItemId() == selected);

        // The target area reaches below the box, so PopupMenu's
        // y = target.getBottom() leaves a gap rather than butting against it.
        CHECK (options.getTargetScreenArea().getBottom() > box.getScreenBounds().getBottom());
        CHECK (options.getTargetScreenArea().getX() == box.getScreenBounds().getX());
        CHECK (options.getMinimumWidth() == box.getWidth());
    }

    box.setLookAndFeel (nullptr);
}

TEST_CASE ("a dropdown inside a dialog opens inside it, not as a window", "[design][dropdown]")
{
    // Every dew dialog is a DialogWindow with a native title bar, so it is a
    // real system window with real system buttons. A PopupMenu on the desktop
    // is another window, and opening one takes key status away from the dialog -
    // at which point macOS greys out its close and zoom buttons and stops them
    // answering. Nothing was hiding them; the dialog had stopped being active
    // because its own dropdown was open.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;
    juce::Label label;
    label.setSize (160, tokens::size::controlHeight);

    // Not added to the desktop: a headless test has no display, and the only
    // thing that matters here is what the box's top-level component IS.
    juce::DialogWindow dialog { "Audio Settings", tokens::colour::background, true, false };
    dialog.setSize (420, 300);

    auto* content = new juce::Component();
    content->setSize (400, 280);

    juce::ComboBox box;
    box.setLookAndFeel (&lookAndFeel);
    box.addItemList ({ "Built-in Output", "Aggregate Device" }, 1);
    box.setSize (160, tokens::size::controlHeight);
    content->addAndMakeVisible (box);

    dialog.setContentOwned (content, false);

    REQUIRE (box.getTopLevelComponent() == &dialog);
    CHECK (lookAndFeel.getOptionsForComboBoxPopupMenu (box, label).getParentComponent() == &dialog);

    // The control case, and the reason this is scoped to dialogs: the main
    // window's own boxes keep a desktop menu, which is free to overflow the
    // window they sit in.
    juce::Component plain;
    plain.setSize (400, 280);

    juce::ComboBox loose;
    loose.setLookAndFeel (&lookAndFeel);
    loose.addItemList ({ "Sine", "Saw" }, 1);
    loose.setSize (160, tokens::size::controlHeight);
    plain.addAndMakeVisible (loose);

    CHECK (lookAndFeel.getOptionsForComboBoxPopupMenu (loose, label).getParentComponent()
           == nullptr);

    box.setLookAndFeel (nullptr);
    loose.setLookAndFeel (nullptr);
}

TEST_CASE ("a disabled dropdown reads as disabled", "[design][dropdown]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    const auto renderBox = [&lookAndFeel] (bool enabled)
    {
        juce::ComboBox box;
        box.setLookAndFeel (&lookAndFeel);
        box.addItem ("Insert 1", 1);
        box.setSelectedId (1, juce::dontSendNotification);
        box.setEnabled (enabled);
        box.setSize (160, tokens::size::controlHeight);

        auto image = render (box);
        box.setLookAndFeel (nullptr);
        return image;
    };

    const auto enabled = renderBox (true);
    const auto disabled = renderBox (false);

    // Mean brightness, not inkCoverage: that compares against the corner pixel,
    // which a rounded control leaves transparent, so it calls almost every
    // pixel "ink" and reports the same number for both.
    const auto meanBrightness = [] (const juce::Image& image)
    {
        double total = 0.0;
        int sampled = 0;

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x, ++sampled)
                total += (double) image.getPixelAt (x, y).getBrightness();

        return sampled > 0 ? total / (double) sampled : 0.0;
    };

    INFO ("brightness enabled " << meanBrightness (enabled) << " disabled "
                                << meanBrightness (disabled));
    REQUIRE (meanBrightness (disabled) < meanBrightness (enabled));
}

TEST_CASE ("a menu opens by animating rather than appearing", "[design][dropdown]")
{
    // Deliberately modest: this asserts the animation is set up and where it
    // ends, which is what can be checked without a message loop. It does not
    // claim anything about the frames in between.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewLookAndFeel lookAndFeel;

    juce::Component window;
    window.setBounds (100, 200, 180, 120);
    const auto target = window.getBounds();

    lookAndFeel.preparePopupMenuWindow (window);

    // Starts transparent and below where it will land.
    REQUIRE (window.getAlpha() < 0.01f);
    REQUIRE (window.getY() > target.getY());
    REQUIRE (window.getY() - target.getY() == tokens::motion::popupRisePx);

    auto& animator = juce::Desktop::getInstance().getAnimator();
    REQUIRE (animator.isAnimating (&window));

    // And it is on its way to exactly where it was told to go.
    animator.cancelAnimation (&window, true);
    REQUIRE (window.getBounds() == target);
    REQUIRE (window.getAlpha() > 0.99f);
}
