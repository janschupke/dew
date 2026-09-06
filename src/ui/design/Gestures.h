#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Tokens.h"

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

    260 was that first choice and it was slow: a full sweep asked for most of a
    laptop trackpad's travel, so setting a cutoff meant two or three drags. 200
    is about a hand's comfortable pull, and shift still divides it by
    fineMultiplier for the times the coarse one is too much.
*/
inline constexpr int dragPixelsForFullRange = 200;

/** Shift, wherever a drag changes a value. */
inline constexpr double fineMultiplier = 0.15;

/** How far the pointer may move before a press becomes a drag. Below this a
    press is a click, so a card still expands when your hand is not quite
    still. */
inline constexpr int dragThresholdPx = 4;

/** How far one wheel notch scrolls, in PIXELS.

    A notch has to mean the same distance on every axis of every view, and it
    meant six different things:

      - six STEPS horizontally, which is 18px zoomed out and 720px zoomed in -
        a fortyfold range on the axis people scroll most
      - one LANE down the playlist, which is 34px or 204px depending on a height
        the same wheel can change
      - three ROWS down the piano roll, which is 42px
      - whatever juce::Viewport does, in the channel rack
      - whatever juce::CodeEditorComponent does, in the score tab
      - and in a number field, nothing at all: it read the SIGN and threw the
        magnitude away

    Each was defensible about itself and none of them agreed. Pixels is the only
    unit all six share, so pixels is the unit, and each handler divides into its
    own at the point of use.

    The value is what a juce::Viewport already does - fourteen times its 16px
    single step - because that is the speed the channel rack scrolls at, and the
    speed everything else on the machine scrolls at. The timelines were about
    five times slower than that, which is the whole of "scrolling feels slow".
    Nothing here depends on JUCE's two numbers; a test asserts the rack still
    agrees, the way the colour ramp is held by a test rather than a dependency.
*/
inline constexpr double wheelPixelsPerNotch = 224.0;

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
    double along() const noexcept
    {
        return x + y;
    }
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

/** What a wheel notch means over a view with two axes.

    The piano roll and the playlist each answered this with the same four-branch
    if/else, and each carried the same comment above the first branch saying
    that a cross-zoom also satisfies isZoom so the order matters. A rule whose
    correctness is a comment repeated in two files is a rule that will be
    reordered in one of them.

    Only the DECISION is shared. What the four mean is not: the roll updates
    both scrollbars once at the end, the playlist re-lays its headers and
    latches that the view is the user's, and their zooms anchor on different
    gutters. Those stay where they are.

    The step grid is deliberately not a caller. It has one axis, so a
    cross-zoom would have nothing to zoom and shift-scroll nothing to switch
    to; two branches written out is clearer there than four with two unused.
*/
enum class WheelIntent
{
    zoomOtherAxis,  ///< rows in the piano roll, lanes in the playlist
    zoomTimeline,   ///< in time, around the pointer
    scrollTimeline, ///< shift: along the timeline whichever way the wheel turned
    scrollBoth      ///< the bare wheel: the other axis, plus any horizontal
};

inline WheelIntent intentOf (const juce::ModifierKeys& mods) noexcept
{
    if (isCrossZoom (mods))
        return WheelIntent::zoomOtherAxis;

    if (isZoom (mods))
        return WheelIntent::zoomTimeline;

    if (mods.isShiftDown())
        return WheelIntent::scrollTimeline;

    return WheelIntent::scrollBoth;
}

/** Finer, for a drag that changes a value. */
inline bool isFine (const juce::ModifierKeys& mods) noexcept
{
    return mods.isShiftDown();
}

/** How far a drag travels to sweep a whole range, with the modifiers applied.

    The two numbers above and the rule joining them, in one place. A primitive
    was combining them by hand - read the flag, divide the distance by the
    multiplier, cast - which is a gesture decision being made in a control, and
    exactly what the rest of this header exists to prevent.

    It also let the gate that guards this rule shrink. That gate had to exempt
    the two files handing JUCE a number, and the exemption list was a list a
    moved file falls off: it went red once already, when the knob left
    DewControls.cpp. A call that NAMES gesture:: needs no exemption at all,
    because it is the opposite of deciding a scale for itself.
*/
inline int dragPixelsFor (const juce::ModifierKeys& mods) noexcept
{
    return isFine (mods) ? (int) ((double) dragPixelsForFullRange / fineMultiplier)
                         : dragPixelsForFullRange;
}

/** How far in from a block's right edge a press RESIZES it rather than moving
    it - a note in the piano roll, a clip in the playlist.

    Both views had their own pair and the pairs had drifted: 8px capped at 35%
    of the width in the piano roll, 10px at 30% in the playlist, for the same
    gesture on the same kind of object. Neither number was in a token, so they
    are float literals in a .cpp and the size-ladder gate cannot see either.

    8 rather than 10 because it is space::md, and the two VERTICAL resize bands
    - a playlist lane's and the effect chain's - are both space::xs already, so
    the horizontal one belonging to the same scale is the whole argument for
    picking between two numbers that were each chosen once and never compared.

    0.35 rather than 0.3 because the fraction is what protects a NARROW block,
    and a one-step note is the narrowest thing either view resizes. The cap
    decides the wide case and the fraction decides the hard one, so each is
    taken from the view that had the better reason for it.
*/
inline float rightEdgeBand (float width) noexcept
{
    return juce::jmin ((float) tokens::space::md, width * 0.35f);
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
