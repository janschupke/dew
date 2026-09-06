#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Animator.h"
#include "ui/design/Focus.h"
#include "ui/design/Tokens.h"

namespace dew
{

/* How a dew control BEHAVES, as opposed to what it looks like.

   The hover easing, the right-button refusal and the focus bookkeeping are the
   three things every control in the design system shares, and they were at the
   top of one 1107-line header that eleven types and a paint namespace also
   lived in - so a change to the knob recompiled every consumer of a checkbox.
*/

/** How much lighter a button's fill gets under the pointer, eased.

    Every button in dew brightened by a step function - at rest, hovered,
    pressed - which is the one place a hard cut is most visible, because the
    pointer is right there. The three states are the same three; only the way
    they are reached changes.

    Driven from Button::buttonStateChanged rather than from paintButton: a
    paint that sets an animation target asks for a repaint from inside a
    repaint, and settles only because animateTo happens to be idempotent.
*/
class ButtonLift
{
public:
    explicit ButtonLift (juce::Button& b)
        : button (b)
        , motion (b)
        , toggled (b)
    {
    }

    /** Call from buttonStateChanged(), which JUCE sends for a change of toggle
        state as well as a change of mouse state. */
    void update();

    /** The fill, lifted by however far the animation has got. */
    juce::Colour apply (juce::Colour base) const;

    /** The same, crossing between an off colour and an on one.

        Mute, solo, arm and bypass all switched colour outright, and those are
        the four states a person flips most often while listening - the one
        moment a hard cut is most likely to be read as a glitch rather than as
        a change.
    */
    juce::Colour apply (juce::Colour off, juce::Colour on) const;

    /** The cross on its own, with no hover lift - for a border or a label,
        which the pointer does not brighten. Without this the fill crossed
        while the outline cut, which reads worse than either. */
    juce::Colour cross (juce::Colour off, juce::Colour on) const;

private:
    juce::Button& button;
    ComponentMotion motion;
    ComponentMotion toggled;
};

/** The right button, refused for the WHOLE press.

    Every control in dew already refused a popup press in mouseDown, and that
    was half a rule. juce::Button re-arms itself on a drag:

        void Button::mouseDrag (const MouseEvent& e)
        {
            updateState (isMouseSourceOver (e), true);   // whichever button
        }

        void Button::mouseUp (const MouseEvent& e)
        {
            const auto wasDown = isDown();
            ...
            if (wasDown && wasOver && ! triggerOnMouseDown)
                internalClickCallback (e.mods);
        }

    So a right-press that moved a single pixel before releasing put the button
    back into buttonDown and the release completed the click. It applied to
    every button in the application - the transport, the tools, both zoom
    groups, delete pattern, and the piano roll's octave pair, which is where it
    was first noticed.

    A LATCH rather than re-reading the modifiers at each phase. The modifiers on
    a release are the modifiers as they are then, and ctrl-click on macOS is a
    popup press whose ctrl may well be up by the time the button comes up - so
    the only reliable statement is the one made at the press and remembered.

    A member rather than MouseEvent::mouseWasDraggedSinceMouseDown, which asks
    the mouse SOURCE and reads false for every synthetic event in the suite.
*/
class PopupPress
{
public:
    /** Call FIRST in mouseDown. True when the press was the right button, in
        which case the control must do nothing else with it. Opens `hook` if
        there is one.

        Swallowing it with no hook is the point and is what this used to get
        wrong: a person aiming at a menu that is not there asked for nothing,
        not for the button.
    */
    bool down (const juce::MouseEvent& event, const std::function<void()>& hook)
    {
        held = event.mods.isPopupMenu();

        if (! held)
            return false;

        if (hook != nullptr)
            hook();

        return true;
    }

    /** Call FIRST in mouseDrag. True while the press that is still down was a
        popup press. */
    bool dragging() const noexcept
    {
        return held;
    }

    /** Call FIRST in mouseUp. True when this release ends a popup press, and
        clears the latch so the next press starts from nothing. */
    bool releasing() noexcept
    {
        const auto was = held;
        held = false;
        return was;
    }

private:
    bool held = false;
};

/** Every control in dew answers `preferredHeight()`.

    Not a JUCE idea - a juce::Component has no intrinsic size and every height
    in this application was decided at the CALL SITE. Which is why a number
    field is 40 inside an effect card, 41 in the randomize dialog and 26 in a
    settings row, sitting beside dropdowns that are always 26; and why the size
    ladder's gate never saw any of it, because it only fires on a `constexpr int
    …Height` whose literal happens to equal a rung.

    A captioned number field is the case that makes this necessary rather than
    tidy: it needs a strip above the value for its caption and an uncaptioned
    one does not, and that is a fact only the field knows. Its callers were
    guessing.

    Free functions would not do: the answer differs per INSTANCE, not per type.
*/

/** Any juce::Button, with the right button refused for the whole press.

    Five controls carried the same three overrides word for word, and the two
    that did NOT are exactly the two a person found: the oscillator slot tabs
    guarded only the press, and the editor tab bar was a stock
    juce::TabbedComponent whose buttons guarded nothing at all. A rule kept by
    copying is a rule that is kept until somebody writes a sixth control.

    internalClickCallback as well as the three phases, because it is the single
    point every completed click passes through - a release, a
    triggerOnMouseDown press, triggerClick, and the space bar. The three phases
    are what stops the gesture; this is what stops anything that gets past them.

    Base is the juce::Button descendant being guarded, so one template serves a
    hand-painted primitive, a stock juce::ToggleButton, a juce::TextButton the
    look and feel hands to a slider, and a juce::TabBarButton the tab bar makes
    for itself.
*/
template <typename Base> class PopupSafeButton : public Base
{
public:
    using Base::Base;

    /** What to offer when this control is right-clicked, or null for nothing.

        A CALLBACK rather than a target or a node, because dew_design "knows
        nothing about a project" - its own CMakeLists says so - and a control
        that held an AutomationTarget would know about one. The editor that
        BUILT this control from a ParamSpec is the one that knows which node it
        was built for, so it is the one that closes over it.
    */
    std::function<void()> onContextMenu;

    /** How tall this wants to be. See DewControls' note on intrinsic size. */
    int preferredHeight() const
    {
        return tokens::size::controlHeight;
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (popupPress.down (event, onContextMenu))
            return;

        Base::mouseDown (event);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (popupPress.dragging())
            return;

        Base::mouseDrag (event);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (popupPress.releasing())
            return;

        Base::mouseUp (event);
    }

protected:
    /** The latch, not the modifiers. A click completed from the keyboard while
        ctrl happens to be held is still a click, so asking the event again here
        would refuse a gesture nobody made with the mouse. */
    void internalClickCallback (const juce::ModifierKeys& mods) override
    {
        if (popupPress.dragging())
            return;

        Base::internalClickCallback (mods);
    }

private:
    PopupPress popupPress;
};

/** A button dew PAINTS itself: the right-button refusal, plus the hover lift
    and the focus note that go with drawing your own.

    Three classes carried these two overrides and the ButtonLift beside them
    word for word - DewButton, DewIconButton, DewLetterToggle - and two of the
    three said so in a comment pointing at the first, which is duplication
    acknowledged in prose rather than removed.

    DewCheckbox is deliberately NOT one of these, though it is a
    PopupSafeButton. DewLookAndFeel paints it, reads no lift, and would repaint
    on every hover for a brightening nobody draws.
*/
template <typename Base> class PaintedButton : public PopupSafeButton<Base>
{
public:
    using PopupSafeButton<Base>::PopupSafeButton;

    /** Tells the design system what moved the keyboard here, so a ring is drawn
        for a tab and not for a click - see focus::ringVisible.

        Above the base's own handling rather than instead of it: the base
        repaints, and the note has to happen first so the repaint it schedules
        paints the answer.
    */
    void focusGained (juce::Component::FocusChangeType cause) override
    {
        focus::noteFocusChange (cause);
        Base::focusGained (cause);
    }

protected:
    void buttonStateChanged() override
    {
        lift.update();
    }

    /** Protected rather than private: it exists to be read by the paintButton
        that each of the three writes for itself, which is the one thing about
        them that genuinely differs. */
    ButtonLift lift { *this };
};

// -----------------------------------------------------------------------------

/** Makes mouse events that land on a child widget also reach `parent`.

    Used for hover: a row should light up while the pointer is over one of its
    buttons, and without this the row only ever sees the pointer leave.

    Deliberately NOT the mechanism for selection. JUCE delivers these through
    ComponentPeer, which cannot be driven in a headless test, so anything that
    matters is wired explicitly instead - a fader that selects its strip does it
    through its own onDragStart, which a test can verify exists and works.
*/
void forwardChildMouseEventsTo (juce::Component& parent);

/** Whether this event happened ON `self` rather than on a child that forwards
    to it.

    The other half of forwardChildMouseEventsTo, and the half that was missing.
    Forwarding is for HOVER - a row should light up while the pointer is over
    one of its buttons - but JUCE delivers a forwarded press to the listener as
    well as to the control, and three rows in dew opened a context menu from
    their own mouseDown. So a right-click on a mixer strip's fader, its pan knob
    or its M and S opened the CONTROL's parameter menu and the STRIP's menu, one
    on top of the other, and the channel rack did the same.

    eventComponent rather than a hit test: it is what JUCE already knows and it
    stays right when a control moves.
*/
inline bool isOwnPress (const juce::MouseEvent& event, const juce::Component& self) noexcept
{
    return event.eventComponent == &self;
}

} // namespace dew
