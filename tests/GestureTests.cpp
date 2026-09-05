#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Gestures.h"
#include "ui/design/Keys.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/DewNumberField.h"

using namespace dew;

namespace
{

juce::MouseWheelDetails wheelOf (float x, float y, bool reversed)
{
    juce::MouseWheelDetails wheel;
    wheel.deltaX = x;
    wheel.deltaY = y;
    wheel.isReversed = reversed;
    wheel.isSmooth = false;
    wheel.isInertial = false;
    return wheel;
}

} // namespace

TEST_CASE ("a wheel notch is read the way the system reports it", "[ui][gesture]")
{
    // JUCE REPORTS natural scrolling rather than applying it, so a handler that
    // ignores isReversed scrolls backwards for anyone running the Mac default.
    // The piano roll was the only one of the three timeline views that read it,
    // which is why the sequencer and the playlist went the wrong way.
    const auto forward = gesture::deltaOf (wheelOf (0.0f, 0.5f, false));
    const auto reversed = gesture::deltaOf (wheelOf (0.0f, 0.5f, true));

    CHECK (forward.y > 0.0);
    CHECK (reversed.y < 0.0);
    CHECK (juce::approximatelyEqual (forward.y, -reversed.y));

    // Both axes, and both flip together - a trackpad reports diagonal movement
    // and reversing only one of them scrolls a view sideways as you swipe up.
    const auto diagonal = gesture::deltaOf (wheelOf (0.25f, 0.5f, true));
    CHECK (diagonal.x < 0.0);
    CHECK (diagonal.y < 0.0);

    // A view that scrolls one way from both axes adds them.
    CHECK (
        juce::approximatelyEqual (gesture::deltaOf (wheelOf (0.25f, 0.5f, false)).along(), 0.75));
}

TEST_CASE ("shift is finer wherever a drag changes a value", "[ui][gesture]")
{
    // Shift already means five other things in dew - suspend snap, extend a
    // selection, make a copy unique, transpose by an octave - and every one of
    // them changes a selection or a POSITION. None changes a value. That is
    // what keeps the sixth meaning from being one too many.
    CHECK (gesture::isFine (juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
    CHECK_FALSE (gesture::isFine (juce::ModifierKeys()));

    // And finer means finer, not coarser or the same.
    CHECK (gesture::fineMultiplier > 0.0);
    CHECK (gesture::fineMultiplier < 1.0);

    // The two of them applied, which is what a control actually asks for.
    // Nothing covered this while the knob was doing the arithmetic itself.
    //
    // A FURTHER drag for the same range is what "finer" means to JUCE: the
    // sensitivity is a distance, so more pixels is less value per pixel.
    CHECK (gesture::dragPixelsFor (juce::ModifierKeys()) == gesture::dragPixelsForFullRange);
    CHECK (gesture::dragPixelsFor (juce::ModifierKeys (juce::ModifierKeys::shiftModifier))
           > gesture::dragPixelsForFullRange);
}

TEST_CASE ("zoom answers to command and to control", "[ui][gesture]")
{
    CHECK (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::commandModifier)));
    CHECK (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
    CHECK_FALSE (gesture::isZoom (juce::ModifierKeys()));
    CHECK_FALSE (gesture::isZoom (juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
}

TEST_CASE ("a press becomes a drag only once it has travelled", "[ui][gesture]")
{
    // Component::getDistanceFromDragStart is fed by the real pointer and reads
    // zero in a headless harness, so this is the only form of the rule a test
    // can reach - which is why the effect card's expand-on-click, which used
    // the Component form, had never been tested at all.
    const juce::Point<int> origin { 40, 40 };

    CHECK_FALSE (gesture::passedThreshold (origin, origin));
    CHECK_FALSE (
        gesture::passedThreshold (origin, origin.translated (gesture::dragThresholdPx - 1, 0)));
    CHECK (gesture::passedThreshold (origin, origin.translated (gesture::dragThresholdPx, 0)));

    // In any direction, not only along an axis.
    CHECK (gesture::passedThreshold (origin, origin.translated (0, -gesture::dragThresholdPx)));
    CHECK (gesture::passedThreshold (origin, origin.translated (10, 10)));
}

TEST_CASE ("an arrow key crosses a value control in a hundred presses", "[ui][gesture][keys]")
{
    // juce::Slider stepped by getInterval(), which ModuleCatalog sets to 0.001
    // on volume, pan, sustain, release and gain: a THOUSAND presses to cross a
    // fader, which is not an editing gesture. This is the fix, and the number
    // is the point of it.
    DewSlider slider;
    slider.setRange (0.0, 1.0, 0.001);
    slider.setValue (0.0, juce::dontSendNotification);

    for (auto i = 0; i < 100; ++i)
        slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 }));

    CHECK (slider.getValue() > 0.99);

    // And it stops at the end rather than wrapping or overshooting.
    for (auto i = 0; i < 50; ++i)
        slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 }));

    CHECK (juce::approximatelyEqual (slider.getValue(), 1.0));

    // Page up is ten of them, so the whole range is ten presses.
    slider.setValue (0.0, juce::dontSendNotification);

    for (auto i = 0; i < 10; ++i)
        slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::pageUpKey, 0 }));

    CHECK (juce::approximatelyEqual (slider.getValue(), 1.0));
}

TEST_CASE ("shift on an arrow refines the step instead of blocking it", "[ui][gesture][keys]")
{
    // juce::Slider::keyPressed returns false the moment ANY modifier is down,
    // so shift did not make the step finer - it stopped the edit happening at
    // all, on every knob and fader in dew. That is the opposite of the one rule
    // Gestures.h states about shift and a value.
    DewSlider slider;
    slider.setRange (0.0, 1.0, 0.001);
    slider.setValue (0.5, juce::dontSendNotification);

    REQUIRE (slider.keyPressed (
        keys::keyPressFor ({ juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier })));

    // Exactly one interval: the finest legal value the control has.
    CHECK (juce::approximatelyEqual (slider.getValue(), 0.501));

    // Control case - the bare arrow from the same place moves ten times as far,
    // so the two are telling apart rather than both landing on the interval.
    slider.setValue (0.5, juce::dontSendNotification);
    slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 }));
    CHECK (juce::approximatelyEqual (slider.getValue(), 0.51));
}

TEST_CASE ("a logarithmic control steps by ratio, not by span", "[ui][gesture][keys]")
{
    // A flat one per cent of 20Hz-18kHz is 180Hz: a leap at the bottom of the
    // range and inaudible at the top. Reading the step off the slider's own
    // NormalisableRange makes a press mean the same musical distance wherever
    // the knob is standing - and makes the keyboard agree with the DRAG on the
    // same control, which is the pair a hand notices.
    DewSlider slider;
    const auto minimum = 20.0, maximum = 18000.0;
    const auto skew = std::log (0.5)
                      / std::log ((std::sqrt (minimum * maximum) - minimum) / (maximum - minimum));

    slider.setNormalisableRange ({ minimum, maximum, 0.0, skew });

    const auto stepFrom = [&slider] (double from)
    {
        slider.setValue (from, juce::dontSendNotification);
        slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 }));
        return slider.getValue();
    };

    const auto low = 100.0, high = 8000.0;
    const auto lowRatio = stepFrom (low) / low;
    const auto highRatio = stepFrom (high) / high;

    // Not EQUAL ratios, and the difference is worth stating rather than
    // tolerating: a juce::NormalisableRange skew is a POWER curve, not the true
    // exponential ParamSpec::fromNormalised uses. The two agree at nought, a
    // half and one and part company in between, which is exactly why the step
    // is read off the slider - so the keyboard is wrong in the same direction
    // as the drag on the same control, rather than right on its own.
    const auto skewedSpread = std::abs (lowRatio - highRatio);
    CHECK (skewedSpread < 0.1);

    // The control case, and the only one that makes the number above mean
    // anything: a flat one per cent of the SPAN is 180Hz, which nearly triples
    // 100Hz and does not touch 8kHz. Without this the test would pass on the
    // linear step it was written to rule out.
    const auto flat = (maximum - minimum) * keys::valueKeys::normalFraction;
    const auto flatSpread = std::abs ((low + flat) / low - (high + flat) / high);

    CHECK (flatSpread > 1.0);
    CHECK (skewedSpread < flatSpread / 10.0);
}

TEST_CASE ("a discrete control never lands between two of its values", "[ui][gesture][keys]")
{
    // One per cent of a nought-to-seven stepper is 0.07, and the snap on the
    // way in would put it straight back where it started: an arrow key that
    // silently does nothing, which is the whole defect in miniature.
    DewSlider octave;
    octave.setRange (0.0, 7.0, 1.0);
    octave.setValue (3.0, juce::dontSendNotification);

    octave.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 }));
    CHECK (juce::approximatelyEqual (octave.getValue(), 4.0));

    octave.keyPressed (keys::keyPressFor ({ juce::KeyPress::downKey, 0 }));
    octave.keyPressed (keys::keyPressFor ({ juce::KeyPress::downKey, 0 }));
    CHECK (juce::approximatelyEqual (octave.getValue(), 2.0));

    // A whole number after a coarse press too, not two and a bit.
    octave.keyPressed (keys::keyPressFor ({ juce::KeyPress::pageUpKey, 0 }));
    CHECK (juce::approximatelyEqual (octave.getValue(), std::round (octave.getValue())));
}

TEST_CASE ("a value control leaves the keys it does not own alone", "[ui][gesture][keys]")
{
    // Home is Rewind and space is Play, from wherever you happen to be looking.
    // A slider that consumed either would shadow the transport for as long as
    // it held the keyboard.
    DewSlider slider;
    slider.setRange (0.0, 1.0, 0.01);

    CHECK_FALSE (slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::homeKey, 0 })));
    CHECK_FALSE (slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::endKey, 0 })));
    CHECK_FALSE (slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::spaceKey, 0 })));
    CHECK_FALSE (slider.keyPressed (keys::keyPressFor ({ juce::KeyPress::escapeKey, 0 })));

    // And ctrl-tab, or the keyboard could never leave the control it is on.
    CHECK_FALSE (slider.keyPressed (
        keys::keyPressFor ({ juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier })));
}

TEST_CASE ("a number field is no longer keyboard-inert", "[ui][gesture][keys]")
{
    // It wanted keyboard focus and drew a focus ring from the day it was
    // written, and answered no key at all - so the tempo, the pattern length
    // and every effect number field could be reached by tab and not edited.
    DewNumberField field;
    field.setRange (20.0, 300.0, 0.1);
    field.setValue (120.0, juce::dontSendNotification);

    REQUIRE (field.keyPressed (keys::keyPressFor ({ juce::KeyPress::upKey, 0 })));
    CHECK (field.getValue() > 120.0);

    const auto afterOne = field.getValue();

    field.setValue (120.0, juce::dontSendNotification);
    field.keyPressed (keys::keyPressFor ({ juce::KeyPress::pageUpKey, 0 }));
    CHECK (field.getValue() > afterOne);

    field.setValue (120.0, juce::dontSendNotification);
    field.keyPressed (
        keys::keyPressFor ({ juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier }));
    CHECK (field.getValue() < afterOne);
    CHECK (field.getValue() > 120.0);

    CHECK_FALSE (field.keyPressed (keys::keyPressFor ({ juce::KeyPress::homeKey, 0 })));
}
