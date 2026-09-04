#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** What the pointer says about what is under it.

    Named for the GESTURE rather than for the arrow, the same way the colours
    are named for their role: a component asks for the cursor that means "you
    can drag this to a new place" and the design system decides which of JUCE's
    stock cursors that is.

    The reason to declare them once is that dew's editors do not agree on where
    a cursor comes from, and cannot. A mixer strip and an effect card are
    components, so they set a cursor and keep it; a playlist clip and a piano
    roll note are NOT components - both editors paint every clip and every note
    into one canvas and hit-test arithmetically - so their cursor is chosen
    inside mouseMove, from the same hit test that decides what a press does.
    Nine call sites had picked a juce::MouseCursor by hand, and two of them
    disagreed about what a draggable thing looks like.

    JUCE does not inherit a cursor from a parent, so every clickable component
    needs its own. That is the other reason for a vocabulary: the alternative is
    twenty scattered spellings of PointingHandCursor.
*/
namespace dew::cursor
{

/** Nothing here answers to the pointer. */
inline const juce::MouseCursor idle { juce::MouseCursor::NormalCursor };

/** A press does something: a button, a dropdown, a checkbox, a ruler, a row. */
inline const juce::MouseCursor clickable { juce::MouseCursor::PointingHandCursor };

/** A vertical drag changes a number: a fader, a knob, a number field, a curve.

    Up means more everywhere in dew, which is what makes one cursor right for
    all four - the gesture is the same gesture.
*/
inline const juce::MouseCursor value { juce::MouseCursor::UpDownResizeCursor };

/** A drag moves this to a new place: a clip, a note, a card being reordered. */
inline const juce::MouseCursor move { juce::MouseCursor::DraggingHandCursor };

/** A drag changes where this ENDS: a clip's or a note's right edge, a trim
    handle, a panel divider. */
inline const juce::MouseCursor resizeX { juce::MouseCursor::LeftRightResizeCursor };

/** A press marks the grid rather than picking something up - the paint and
    slice tools, where the pointer is a nib and not a hand. */
inline const juce::MouseCursor nib { juce::MouseCursor::CrosshairCursor };

} // namespace dew::cursor
