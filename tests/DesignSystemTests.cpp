// The primitives, and the tokens and icons they are built from.
//
// Split out of DesignSystemTests.cpp along the tags it already carried. It had
// no shared fixture to divide - its anonymous namespace was empty.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "ui/design/DewGallery.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Gestures.h"
#include "ui/design/Icons.h"
#include "ui/design/SignalScope.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"
#include "ui/primitives/HoverTracker.h"

#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

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

    SECTION ("a stock rotary spanning zero fills from the centre")
    {
        // A DewKnob is told it is bipolar; a stock juce::Slider cannot be, so
        // the LookAndFeel reads it off the range. Without that, the mixer's pan
        // knob and the instrument panel's drew a half-turned arc at dead centre
        // while the channel rack's, a DewKnob, drew the empty ring they should.
        DewLookAndFeel lookAndFeel;

        const auto arcAtCentre = [&] (double minimum, double maximum)
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::NoTextBox };
            slider.setLookAndFeel (&lookAndFeel);
            slider.setRange (minimum, maximum, 0.001);
            slider.setValue ((minimum + maximum) * 0.5, juce::dontSendNotification);
            slider.setSize (44, 44);
            slider.resized();

            const auto coverage = coverageOf (render (slider), tokens::colour::accent);
            slider.setLookAndFeel (nullptr);
            return coverage;
        };

        // Half a ring of accent for the unipolar one; effectively none for the
        // bipolar one, whose value IS the centre it fills from.
        REQUIRE (arcAtCentre (0.0, 1.0) > 0.02f);
        REQUIRE (arcAtCentre (-1.0, 1.0) < arcAtCentre (0.0, 1.0) * 0.25f);
    }
}

TEST_CASE ("an icon button's role colours its glyph", "[design][primitives]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The whole icon set was one grey. Record, delete and play carry meaning
    // that their SHAPE alone was being asked to hold, which is a lot to ask of
    // a twelve-pixel glyph in a panel that has thirty of them.
    //
    // Measured at 48px rather than the real 24: coverageOf counts pixels within
    // a tolerance, and a glyph inside a 24px button is about forty of them
    // before antialiasing takes its share.
    //
    // Counted rather than compared as a fraction - -Wfloat-equal is an error
    // under the ci preset, and "none of them" is an integer statement anyway.
    const auto glyphPixels = [] (DewIconButton::Role role, juce::Path icon, juce::Colour target)
    {
        DewIconButton button (std::move (icon), "Tip", role);
        button.setSize (48, 48);

        const auto image = render (button);
        auto matching = 0;

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (pixel.getAlpha() > 200
                    && std::abs ((int) pixel.getRed() - (int) target.getRed()) < 24
                    && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) < 24
                    && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) < 24)
                    ++matching;
            }

        return matching;
    };

    CHECK (glyphPixels (DewIconButton::Role::record, icons::record(), tokens::colour::recording)
           > 0);
    CHECK (glyphPixels (DewIconButton::Role::danger, icons::trash(), tokens::colour::danger) > 0);
    CHECK (glyphPixels (DewIconButton::Role::go, icons::play(), tokens::colour::success) > 0);

    // The control case, and the one that says the ROLE is doing the work: the
    // same glyph with no role paints none of those pixels. Without it the three
    // checks above would also pass on a button that painted its whole fill in
    // the target colour.
    CHECK (glyphPixels (DewIconButton::Role::neutral, icons::record(), tokens::colour::recording)
           == 0);
    CHECK (glyphPixels (DewIconButton::Role::neutral, icons::trash(), tokens::colour::danger) == 0);
    CHECK (glyphPixels (DewIconButton::Role::neutral, icons::play(), tokens::colour::success) == 0);
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

        field.mouseDown ({ source,
                           start,
                           {},
                           1.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           &field,
                           &field,
                           now,
                           start,
                           now,
                           1,
                           false });

        const juce::Point<float> moved (60.0f, 13.0f + (float) dy);
        field.mouseDrag ({ source,
                           moved,
                           {},
                           1.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           0.0f,
                           &field,
                           &field,
                           now,
                           start,
                           now,
                           1,
                           true });

        field.mouseUp ({ source,
                         moved,
                         {},
                         1.0f,
                         0.0f,
                         0.0f,
                         0.0f,
                         0.0f,
                         &field,
                         &field,
                         now,
                         start,
                         now,
                         1,
                         true });
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

TEST_CASE ("a knob answers vertical travel, and only vertical travel", "[design][primitives]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The knob used to be RotaryHorizontalVerticalDrag, which is JUCE's default
    // for a rotary and adds the two axes together: `(x - startX) + (startY -
    // y)`. So a hand pulling down while drifting right subtracted its own drift
    // from its own travel, and the same 60px pull landed anywhere between
    // nothing and twice what was asked for depending on which way the hand
    // wandered. Nothing tested it, because nothing dragged a knob at all: the
    // scale was held only by the gate that the call names `gesture::`.
    //
    // This is the guard. The three drags below make the SAME vertical travel.
    DewKnob knob { "Level", 0.0, 1.0, 0.0001 };
    knob.setSize (tokens::size::knob, tokens::size::knobRow);
    knob.resized();

    auto& slider = knob.getSlider();

    // Driven on the slider rather than on the knob, because the slider is what
    // the pointer is actually over and what owns the drag. The knob's own
    // mouseDown only latches shift, and JUCE delivers that through a peer the
    // harness does not have.
    const auto dragBy = [&knob, &slider] (int dx, int dy)
    {
        knob.setValue (0.5, juce::dontSendNotification);

        const juce::Point<float> start (22.0f, 18.0f);
        const juce::Point<float> moved (start.x + (float) dx, start.y + (float) dy);
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();

        const auto at = [&] (juce::Point<float> where, bool dragged)
        {
            return juce::MouseEvent { source,  where,   {},  1.0f,  0.0f, 0.0f, 0.0f,   0.0f,
                                      &slider, &slider, now, start, now,  1,    dragged };
        };

        slider.mouseDown (at (start, false));
        slider.mouseDrag (at (moved, true));
        slider.mouseUp (at (moved, true));

        return knob.getValue();
    };

    // Up is more, and the distance is the travel over the one shared scale.
    const auto expected = 0.5 + 60.0 / (double) gesture::dragPixelsForFullRange;

    CHECK_THAT (dragBy (0, -60), Catch::Matchers::WithinAbs (expected, 0.002));

    // Drifting either way changes nothing. Under the old style the first of
    // these landed on 0.5 exactly - the drift cancelled the pull - and the
    // second moved twice as far as the hand had travelled.
    CHECK_THAT (dragBy (60, -60), Catch::Matchers::WithinAbs (expected, 0.002));
    CHECK_THAT (dragBy (-60, -60), Catch::Matchers::WithinAbs (expected, 0.002));

    // And down is less, which is the half a value control cannot get wrong.
    CHECK (dragBy (0, 60) < 0.5);
}

TEST_CASE ("hover is tracked once, and the right way round", "[ui][design]")
{
    // The rack row's exit handler read `setHovered (! isMouseOver (true))`, so
    // it lit up when the pointer left and went dark when it arrived - the same
    // three lines as the mixer strip's and the effect card's, negated.
    juce::Component owner;
    owner.setSize (100, 30);

    dew::HoverTracker hover { owner };
    auto changes = 0;
    hover.onChange = [&changes] { ++changes; };

    REQUIRE_FALSE (hover.isHovered());

    hover.enter();
    CHECK (hover.isHovered());
    CHECK (changes == 1);

    // Idempotent: mouseEnter fires again when the pointer crosses back from a
    // child, and re-stating the same value must not cost a repaint.
    hover.enter();
    CHECK (hover.isHovered());
    CHECK (changes == 1);

    // Nothing is under the pointer in a headless harness, so exit means gone.
    hover.exit();
    CHECK_FALSE (hover.isHovered());
    CHECK (changes == 2);

    hover.exit();
    CHECK (changes == 2);
}

TEST_CASE ("the channel ramp says the same thing in both layers", "[design][model]")
{
    // The ramp is stated twice on purpose. The document layer needs it without
    // depending on the design system - a .dew file stores a colour, and
    // dew_model must not link the UI's vocabulary to write one - and the design
    // system needs it for painting a channel that has no stored colour.
    //
    // Two declarations and no dependency is a deliberate trade, and this is the
    // half of it that has to be paid: they are compared here, so a palette
    // change that touches one and not the other is a failing test rather than a
    // rack whose rows disagree with its grid.
    REQUIRE (
        dew::entityColour::rampSize()
        == (int) (sizeof (tokens::colour::channelRamp) / sizeof (tokens::colour::channelRamp[0])));

    for (int i = 0; i < dew::entityColour::rampSize(); ++i)
    {
        INFO ("ramp entry " << i);
        REQUIRE (juce::Colour::fromString (dew::entityColour::defaultHex (i))
                 == tokens::colour::channelColour (i));
    }

    // Both wrap, and both wrap the same way, so channel 9 and channel 1 are one
    // colour in the document and one colour on screen.
    REQUIRE (dew::entityColour::defaultHex (8) == dew::entityColour::defaultHex (0));
    REQUIRE (dew::entityColour::defaultHex (-1) == dew::entityColour::defaultHex (7));
}

TEST_CASE ("a channel with no colour still has one", "[design][model]")
{
    // Three of the five copied read expressions produced transparent black from
    // a missing property, which paints as nothing at all - a channel with no
    // stripe, no clip fill and no note colour, and no clue why.
    juce::ValueTree channel { dew::ids::CHANNEL };
    CHECK (dew::entityColour::of (channel).isOpaque());

    // Length is not validity - "not a colour" also ends in six characters, and
    // juce reads a non-hex digit as a zero, so this used to come back as an
    // almost-black that looked like a deliberate choice.
    channel.setProperty (dew::ids::colour, "not a colour", nullptr);
    CHECK (dew::entityColour::of (channel) == tokens::colour::channelColour (0));

    // And a real value reads back exactly, in either stored spelling.
    channel.setProperty (dew::ids::colour, "ff29a19c", nullptr);
    CHECK (dew::entityColour::of (channel) == tokens::colour::channelColour (1));

    channel.setProperty (dew::ids::colour, "29a19c", nullptr);
    CHECK (dew::entityColour::of (channel) == tokens::colour::channelColour (1));
}
