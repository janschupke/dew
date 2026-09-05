#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::focus
{

/** Whether a focus ring should be DRAWN, which is not the same question as
    whether a control has the keyboard.

    Every primitive ended its paint with

        paint::focusRing (g, *this, hasKeyboardFocus (true))

    so clicking a knob left an accent ring around it until something else was
    clicked. A ring answers "where will the next keystroke go", and that is a
    question you only have while your hands are on the keyboard: a pointer
    already says where the next click goes, by being where it is. Ringing what
    the mouse just pressed is the app repeating back what the user did, in the
    one colour it also uses for selection.

    ONE flag for the whole application rather than a bit per control, because
    only one thing holds the keyboard at a time - so the last focus change is
    the only one whose CAUSE can still matter. It is the same idea the web
    spells :focus-visible, and the same shape as theme::active: a piece of state
    the design system holds and paint() reads.

    A plain variable rather than juce::Desktop::addGlobalMouseListener, and
    JUCE's own FocusChangeType rather than a guess at what the last input was,
    for the reason that shapes everything else in this directory: dew's UI tests
    paint into an Image with no ComponentPeer and no mouse source, so anything
    routed through Desktop cannot be driven where the suite runs. A primitive
    says what moved the focus onto it; a test says the same thing in one call.
*/
namespace detail
{
inline bool pointerMovedFocus = false;
}

/** What a primitive calls from focusGained, or from
    focusOfChildComponentChanged where the focus lives on a child - a DewKnob's
    slider, a DewNumberField's editor.

    focusChangedDirectly is deliberately read as "not the pointer". The three
    canvases call grabKeyboardFocus from their own mouseDown and arrive here as
    `directly`, but a canvas draws paint::cursorOutline rather than a ring, and
    CanvasCursor::isPlaced already refuses to draw until the keyboard has placed
    it. So the case exists and paints nothing either way.
*/
inline void noteFocusChange (juce::Component::FocusChangeType cause) noexcept
{
    detail::pointerMovedFocus = cause == juce::Component::focusChangedByMouseClick;
}

/** Whether a control that HAS the keyboard should also say so. */
inline bool ringVisible() noexcept
{
    return ! detail::pointerMovedFocus;
}

/** Both halves of the question a primitive actually asks.

    Takes the flag rather than reading it off the component for the reason
    paint::focusRing does: grabKeyboardFocus does nothing without a
    ComponentPeer, so a helper that asked for itself could never be shown to
    draw.
*/
inline bool ringVisibleFor (bool hasFocus) noexcept
{
    return hasFocus && ringVisible();
}

} // namespace dew::focus
