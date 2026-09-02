#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/DewLookAndFeel.h"
#include "ui/design/DewGallery.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

using namespace dew;

namespace
{

juce::Image render (juce::Component& c)
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
float coverageOf (const juce::Image& image, juce::Colour target)
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

float inkCoverage (const juce::Image& image)
{
    const auto background = image.getPixelAt (0, 0);
    int differing = 0, sampled = 0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x, ++sampled)
            if (image.getPixelAt (x, y) != background)
                ++differing;

    return sampled > 0 ? (float) differing / (float) sampled : 0.0f;
}

} // namespace

TEST_CASE ("every icon draws something inside its bounds", "[design][icons]")
{
    // An icon that has been declared but returns an empty path draws nothing and
    // would otherwise only be noticed by someone looking at a button.
    for (const auto& icon : icons::all())
    {
        const auto path = icon.make();

        INFO ("icon: " << icon.name);
        REQUIRE (! path.isEmpty());

        const auto bounds = path.getBounds();
        REQUIRE (bounds.getWidth() > 0.0f);
        REQUIRE (bounds.getHeight() > 0.0f);

        // Drawn into a real image, it must actually mark pixels at button size.
        juce::Image image (juce::Image::ARGB, 24, 24, true);
        {
            juce::Graphics g (image);
            icons::draw (g, path, juce::Rectangle<float> (0.0f, 0.0f, 24.0f, 24.0f),
                         juce::Colours::white);
        }

        int marked = 0;

        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x)
                if (image.getPixelAt (x, y).getAlpha() > 0)
                    ++marked;

        INFO ("marked pixels: " << marked);
        REQUIRE (marked > 8);
    }
}

TEST_CASE ("icon names are unique", "[design][icons]")
{
    juce::StringArray names;

    for (const auto& icon : icons::all())
        names.add (icon.name);

    const auto before = names.size();
    names.removeDuplicates (true);

    REQUIRE (names.size() == before);
}

TEST_CASE ("the colour ramp cycles and never returns a transparent colour", "[design][tokens]")
{
    for (int i = -20; i < 40; ++i)
    {
        const auto c = tokens::colour::channelColour (i);
        INFO ("index " << i);
        REQUIRE (c.getAlpha() == 255);
    }

    // Wrapping, including for negative indices.
    REQUIRE (tokens::colour::channelColour (0) == tokens::colour::channelColour (8));
    REQUIRE (tokens::colour::channelColour (-1) == tokens::colour::channelColour (7));
}

TEST_CASE ("primitives paint in every state", "[design][primitives]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("buttons")
    {
        for (auto role : { DewButton::Role::normal, DewButton::Role::primary,
                           DewButton::Role::ghost, DewButton::Role::danger })
        {
            DewButton button ("Label", role);
            button.setSize (100, tokens::size::controlHeight);

            REQUIRE (inkCoverage (render (button)) > 0.0f);

            button.setEnabled (false);
            REQUIRE (inkCoverage (render (button)) > 0.0f);
        }
    }

    SECTION ("icon button")
    {
        DewIconButton button (icons::play(), "Play");
        button.setSize (24, 24);
        REQUIRE (inkCoverage (render (button)) > 0.0f);
    }

    SECTION ("letter toggle reflects its state")
    {
        DewLetterToggle toggle ("M", tokens::colour::warning, "Mute");
        toggle.setSize (26, 20);

        const auto off = render (toggle);
        toggle.setToggleState (true, juce::dontSendNotification);
        const auto on = render (toggle);

        // Different states must look different, or the control tells the user
        // nothing.
        REQUIRE (off.getPixelAt (13, 10) != on.getPixelAt (13, 10));
    }

    SECTION ("knob")
    {
        DewKnob knob ("CUTOFF", 0.0, 1.0, 0.001);
        knob.setSize (72, 78);
        knob.resized();

        knob.setValue (0.0, juce::dontSendNotification);
        const auto low = render (knob);

        knob.setValue (1.0, juce::dontSendNotification);
        const auto high = render (knob);

        REQUIRE (inkCoverage (low) > 0.0f);

        // Total ink barely moves - the track is always drawn - so the check has
        // to be on the value arc specifically.
        REQUIRE (coverageOf (high, tokens::colour::accent)
                 > coverageOf (low, tokens::colour::accent));
    }
}

TEST_CASE ("the number field changes by dragging, and up means more", "[design][primitives]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewNumberField field;
    field.setRange (20.0, 300.0, 0.1);
    field.setValue (128.0, juce::dontSendNotification);
    field.setSize (120, tokens::size::controlHeight);

    int edits = 0;
    field.onValueChange = [&edits] { ++edits; };

    const auto drag = [&field] (int dy)
    {
        const juce::Point<float> start (60.0f, 13.0f);
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();

        field.mouseDown ({ source, start, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &field, &field, now, start, now, 1, false });

        const juce::Point<float> moved (60.0f, 13.0f + (float) dy);
        field.mouseDrag ({ source, moved, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           &field, &field, now, start, now, 1, true });

        field.mouseUp ({ source, moved, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &field, &field, now, start, now, 1, true });
    };

    // Dragging UP increases, which is the convention everywhere and the opposite
    // of the screen's y axis.
    drag (-100);
    REQUIRE (field.getValue() > 128.0);
    REQUIRE (edits > 0);

    const auto afterUp = field.getValue();
    drag (100);
    REQUIRE (field.getValue() < afterUp);

    // And it clamps rather than running away.
    drag (-100000);
    REQUIRE (field.getValue() <= 300.0);

    drag (100000);
    REQUIRE (field.getValue() >= 20.0);
}

TEST_CASE ("the gallery lays out and paints every section", "[design][gallery]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewGallery gallery;
    gallery.setVisible (true);
    gallery.setSize (1200, 400);        // deliberately too short
    gallery.setSize (1200, gallery.getRequiredHeight());

    REQUIRE (gallery.getRequiredHeight() > 400);

    const auto image = render (gallery);
    REQUIRE (inkCoverage (image) > 0.05f);

    // The last section must actually be inside the reported height: a gallery
    // that clips its own content is worse than none.
    const auto bottomStrip = image.getClippedImage (
        { 0, gallery.getRequiredHeight() - 120, image.getWidth(), 100 });

    REQUIRE (inkCoverage (bottomStrip) > 0.0f);
}

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
    const auto chevronStrip = image.getClippedImage ({ image.getWidth() - 30, 0, 28, image.getHeight() });
    INFO ("chevron ink: " << inkCoverage (chevronStrip));
    REQUIRE (inkCoverage (chevronStrip) > 0.02f);

    box.setLookAndFeel (nullptr);
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

    INFO ("brightness enabled " << meanBrightness (enabled)
          << " disabled " << meanBrightness (disabled));
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
