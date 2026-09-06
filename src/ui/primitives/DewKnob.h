#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ParamSpec.h"
#include "ui/design/Animator.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/ButtonBehaviour.h"
#include "ui/primitives/TypedEdit.h"

namespace dew
{

/* The rotary, and the slider it is built on.

   DewKnob already had its own .cpp; this is the header that should have come
   with it. DewSlider comes too, because a knob IS one and nothing else in the
   tree builds a bare juce::Slider without wanting the same right-button
   refusal.
*/

/** juce::Slider, minus the defect that a right-drag moves the value.

    The same rule the buttons follow, applied to the one JUCE control dew builds
    on directly. juce::Slider only treats a right press as a menu when
    setPopupMenuEnabled is on, and nothing in dew turns it on - so the press
    fell through to the drag branch, armed a gesture and moved the value. A
    right-drag on a knob, on a mixer fader or on an octave stepper wrote an undo
    step for a gesture nobody asked for.

    A subclass rather than setPopupMenuEnabled, because JUCE's own menu is
    "Velocity mode" and "Rotary mode", which are not dew's vocabulary and would
    appear in a design system that owns every other menu in the application.

    Everything else is stock: DewLookAndFeel already draws the rotary, the
    linear track and the text box, so what the control lacked was not an
    appearance but a refusal.
*/
class DewSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    /** Arrows, shift-arrows and page up/down, and NEVER juce::Slider's.

        The base handler steps by getInterval(), which the catalog sets to 0.001
        on volume, pan, sustain, release and gain - a thousand presses to cross
        a fader. And it returns false the moment any modifier is down
        (juce_Slider.cpp:1031), so shift did not refine the step, it blocked the
        edit: the opposite of the one rule Gestures.h states about shift and a
        value. Both halves are wrong, so there is nothing left to delegate to.

        Anything else returns false, so space still plays, home still rewinds
        and ctrl-tab still leaves the control.
    */
    bool keyPressed (const juce::KeyPress& key) override;

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (popupPress.down (event, nullptr))
            return;

        juce::Slider::mouseDown (event);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (popupPress.dragging())
            return;

        juce::Slider::mouseDrag (event);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (popupPress.releasing())
            return;

        juce::Slider::mouseUp (event);
    }

    /** A SIDEWAYS notch is not for the value. It scrolls whatever the slider is
        sitting in.

        juce::Slider::mouseWheelMove returns true for any notch on any style
        with the wheel enabled, so the event never reaches the Viewport above it
        - and juce_Slider.cpp picks the dominant axis, taking -deltaX when the
        horizontal component wins. So swiping sideways across the mixer, over a
        fader, moved that fader's gain instead of scrolling the row: the one
        gesture whose whole purpose is to reach the strip you cannot see.

        The value keeps the vertical notch, which is the one a wheel sends and
        the one a person means on a fader. Only the horizontal-dominant case is
        handed upwards, and handed rather than swallowed - Component's own
        implementation is what walks up to the Viewport.

        Read through gesture::deltaOf because a view that reads wheel.deltaX for
        itself is exactly what the gesture gate refuses; three views once had
        three ideas about which way a notch pointed.
    */
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const auto delta = gesture::deltaOf (wheel);

        if (std::abs (delta.x) > std::abs (delta.y))
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        juce::Slider::mouseWheelMove (event, wheel);
    }

private:
    PopupPress popupPress;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewSlider)
};

/** A rotary with its caption and value drawn as one unit, so knobs line up
    without every caller laying out a separate label.
*/
class DewKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    DewKnob (const juce::String& caption, double minimum, double maximum, double interval);

    /** A knob built from what the catalog declares the parameter to be: its
        caption, its range, its step, its decimals, whether it is bipolar and
        whether it sweeps logarithmically.

        Six knobs and a stepper stated those a second time by hand, and every
        one of them had drifted from the engine's own clamp.
    */
    explicit DewKnob (const ParamSpec&);

    void setValue (double, juce::NotificationType = juce::sendNotification);
    double getValue() const noexcept
    {
        return slider.getValue();
    }

    void setNumDecimalPlaces (int);

    /** Bipolar knobs fill out from the centre - pan, detune, EQ gain. */
    void setBipolar (bool);

    /** What this control DOES, as the colour of its value arc.

        Set for you by the ParamSpec constructor, which is why almost nothing
        calls this: every knob in the application is built from the catalog, so
        every knob is already the right colour. It is here for the two controls
        that are not - a free-form knob, and a stock juce::Slider that has to be
        told through Slider::trackColourId instead.
    */
    void setFunctionColour (juce::Colour);

    /** Drops the caption and the value readout and gives the rotary the whole
        component, for a knob that has to fit on a 34px row. The caption then has
        nowhere to be drawn, so a compact knob says what it is through its
        tooltip and through being bipolar or not.
    */
    void setCompact (bool);

    /** Sets it on BOTH this and the slider inside.

        The slider, because juce::TooltipWindow hit-tests the deepest component
        under the pointer and that is what the pointer is actually over. And on
        this, because the status bar's hover help walks UP from wherever the
        event landed - a knob whose tooltip lived only on its child was a knob
        that could not be found from outside, which is how six of them reached
        the window with nothing to say.
    */
    void setTooltip (const juce::String&) override;

    /** IGNORED, not inaccessible.

        A DewKnob is a juce::Component wrapping the juce::Slider that is the
        actual control, and the slider is what carries the role, the range and
        the value a screen reader reads out. Left alone the wrapper announces
        itself as an unnamed group containing one slider.

        setAccessible (false) would be the wrong tool: Component::isAccessible
        walks UP to its parent, so switching the wrapper off takes the slider
        inside it off too. An ignored handler is the one that means "skip me,
        keep my children".
    */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** Where along its travel the value sits, 0..1 - which is where the needle
        is drawn and where the drag put it. A test seam: the two were computed
        by two different functions and disagreed on every curved parameter. */
    float getValueProportion() const
    {
        return proportionOfValue();
    }

    juce::Slider& getSlider() noexcept
    {
        return slider;
    }

    std::function<void()> onValueChange;

    /** A drag begins and ends. Both halves matter: a caller opens one undo
        transaction on the first and stops re-opening it on the second, which is
        what makes a whole gesture a single undo step.
    */
    std::function<void()> onEditStart;
    std::function<void()> onEditEnd;

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Shift at the moment of PRESS makes the whole drag a fine one.

        Fixed for the gesture rather than live, because JUCE computes a rotary's
        value from where the drag began and the current sensitivity: changing
        it mid-drag rescales the travel already made and the knob jumps.
    */
    void mouseDown (const juce::MouseEvent&) override;

    /** Double-click to type an exact value, the way a number field already
        could.

        A knob's readout is DRAWN text rather than a control, so until this
        there was no way to give a knob a number at all - the only route to
        1400 Hz was to drag until it happened to say 1400. The box shows a
        plain number with no unit: a readout carries its suffix, and a box
        seeded with "0.140 s" is a box whose contents do not parse.

        A compact knob has no readout to double-click and gets none of this.
    */
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** focusOfChildComponentChanged rather than focusGained, because a knob is a
        Component wrapping the juce::Slider that carries the range, the value and
        the keyboard. The ring is drawn around the wrapper and the focus is on
        the child, so this is the only hook that fires. */
    void focusOfChildComponentChanged (FocusChangeType cause) override
    {
        focus::noteFocusChange (cause);
        repaint();
    }

private:
    /** Where the needle actually is, which is not always where the value is.

        Three rules, in priority order, and each of them is load-bearing:

          1. Animation is off unless the application turns it on. That is what
             keeps a headless render of a knob a render of the value it was
             given.
          2. A DRAG is never eased. Easing a control against the pointer that is
             dragging it feels broken, because the two disagree the whole way.
          3. The FIRST value a knob is ever given snaps. A panel built from a
             document must not sweep every knob up from zero.

        The numeric readout is not eased. A number is read and a needle is seen;
        a readout that arrived 120ms late would just look wrong.
    */
    void updateNeedle();
    float proportionOfValue() const;

    juce::String caption;

    /** The unit, from the ParamSpec. Empty for a knob built without one, which
        is every knob whose value is a bare 0..1 proportion. */
    juce::String suffix;

    /** Accent until a ParamSpec says otherwise, so a knob built without one
        looks exactly as every knob used to. */
    juce::Colour functionColour { tokens::colour::accent };

    /** RotaryVerticalDrag: the value is what the pointer travelled UP, and
        nothing else.

        RotaryHorizontalVerticalDrag - JUCE's default for a rotary and what this
        was - adds the two axes together: `(x - startX) + (startY - y)`. So a
        hand pulling down and drifting right subtracts its own drift from its
        own travel, and the knob answers a diagonal drag with less than the
        distance it made. That reads as a knob that is not listening, and it is
        the one thing a value control cannot be.
    */
    /** Where the typed box goes: the readout, opened out to a control's height
        so what is typed into it is legible. A knob's value band is sixteen
        pixels, which is a number you can read and not a box you can type in. */
    juce::Rectangle<int> typedEditBounds() const;

    DewSlider slider { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    TypedEdit typed { *this };
    ComponentMotion needle { *this };
    bool needleSeeded = false;
    bool dragging = false;
    int decimalPlaces = 3;
    bool bipolar = false;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DewKnob)
};

} // namespace dew
