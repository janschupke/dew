#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew::gesture
{

/** How dew reads the mouse.

    Not vocabulary - behaviour. A knob, a fader, a number field and three
    timelines all answer to a drag, a wheel and a modifier, and each had its own
    idea of how far a drag goes and how fast a notch scrolls. Three wheel
    speeds (4, 6 and 8 steps a notch), one view of three honouring the system's
    natural-scrolling flag, and nobody calling setMouseDragSensitivity at all,
    so every knob silently sat on JUCE's default.

    The keyboard half of this used to live here too. It moved to ui/Hotkeys.h
    once the application's own key table had to join it: a map that knew about
    the digits but not about cmd-digit was exactly how cmd-1 got swallowed.

    Two rules live here as much as the numbers do:

      - **Shift means finer, wherever a drag changes a VALUE.** Shift already
        means five other things in dew - suspend snap, extend a selection, make
        a copy unique, transpose by an octave - and every one of them changes a
        selection or a position. None changes a value. That is the line that
        keeps the sixth meaning from being one too many.

      - **The wheel is never eased.** Scrolling is the one gesture that has to
        feel directly connected to the hand; animating it adds lag to the only
        thing the pointer is doing.
*/

/** How far a drag travels to sweep a control's whole range.

    JUCE's default is 250 and nothing set it, which is not the same as choosing
    250: it means a knob on a dense mixer strip and a knob in a panel behaved
    identically to a slider nobody had thought about.
*/
inline constexpr int dragPixelsForFullRange = 260;

/** Shift, wherever a drag changes a value. */
inline constexpr double fineMultiplier = 0.15;

/** How far the pointer may move before a press becomes a drag. Below this a
    press is a click, so a card still expands when your hand is not quite
    still. */
inline constexpr int dragThresholdPx = 4;

/** Steps of the timeline per wheel notch. */
inline constexpr double wheelStepsPerNotch = 6.0;

/** How hard a wheel notch zooms. deltaY is small, and a zoom that moved by it
    directly would take a dozen notches to be noticeable. */
inline constexpr double wheelZoomExponent = 3.0;

/** A wheel movement with the system's natural-scrolling setting applied.

    JUCE REPORTS `isReversed` rather than applying it, so a handler that ignores
    it scrolls backwards for anyone running the Mac default - which the
    sequencer and the playlist both did.
*/
struct WheelDelta
{
    double x = 0.0;
    double y = 0.0;

    /** What a scroll of both axes amounts to along one. */
    double along() const noexcept { return x + y; }
};

inline WheelDelta deltaOf (const juce::MouseWheelDetails& wheel) noexcept
{
    const auto direction = wheel.isReversed ? -1.0 : 1.0;

    return { (double) wheel.deltaX * direction, (double) wheel.deltaY * direction };
}

/** The zoom modifier: command on macOS, control elsewhere, and both accepted
    everywhere so a shared muscle memory works either way. */
inline bool isZoom (const juce::ModifierKeys& mods) noexcept
{
    return mods.isCommandDown() || mods.isCtrlDown();
}

/** The zoom modifier plus shift: the OTHER axis.

    A timeline zooms in time, and time is horizontal in all three views, so the
    axis a lane is measured on had no wheel gesture at all. Shift is the axis
    switch on the plain wheel already - it scrolls time where a bare wheel
    scrolls rows - so shift-plus-zoom meaning "zoom the rows" is the same
    distinction applied to the same modifier rather than a sixth meaning for it.

    Must be tested BEFORE isZoom, which it also satisfies.
*/
inline bool isCrossZoom (const juce::ModifierKeys& mods) noexcept
{
    return isZoom (mods) && mods.isShiftDown();
}

/** Finer, for a drag that changes a value. */
inline bool isFine (const juce::ModifierKeys& mods) noexcept
{
    return mods.isShiftDown();
}

/** A right-drag erases in the two grids. In the playlist it opens a menu
    instead, and deliberately: a clip is an object with properties and a step
    is not. */
inline bool isErase (const juce::ModifierKeys& mods) noexcept
{
    return mods.isPopupMenu();
}

/** Whether a press has travelled far enough to be a drag.

    Component::getDistanceFromDragStart is always zero in a headless harness,
    because it is fed by the real pointer, so a component that wants this
    tested has to keep its own origin - which is what PianoRollComponent
    already does and what the effect card now does.
*/
inline bool passedThreshold (juce::Point<int> origin, juce::Point<int> now) noexcept
{
    return origin.getDistanceFrom (now) >= dragThresholdPx;
}

} // namespace dew::gesture
