#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** Where the keyboard is on a canvas that PAINTS its contents.

    The piano roll, the playlist and the step grid draw their notes, clips and
    cells rather than parenting them, so there is nothing for the keyboard to
    land on and nothing for a screen reader to meet. A cursor is the answer both
    need: a coordinate the arrow keys move, drawn where it is and said out loud
    when it moves.

    A COORDINATE, deliberately, and never a juce::ValueTree. Two reasons, and
    the second is the one that would have cost a day:

      - A position survives an edit. Delete the note under the cursor and the
        cursor is still somewhere; hold the note instead and it is holding a
        tree that is no longer in the document.
      - ProjectEdits::moveClipToTrack returns a NEW tree and detaches the one it
        was given, so a cursor holding a clip would be pointing at a corpse the
        moment somebody dragged it to another track.

    What the two axes MEAN is the view's business - step and pitch in the roll,
    bar and track in the playlist, step and row in the grid - and so is what
    lives at a coordinate. This only knows how to be somewhere, and how to stay
    inside the limits it is given.

    Not the selection. The cursor is where you are; the selection is what you
    have chosen. The step grid has no selection at all and still wants one of
    these.
*/
class CanvasCursor
{
public:
    /** False until something places it, so a view that has never been reached
        by the keyboard draws nothing. */
    bool isPlaced() const noexcept
    {
        return placed;
    }

    juce::Point<int> getPosition() const noexcept
    {
        return position;
    }

    /** Puts it somewhere, clamped into `limits`. True if it ended up somewhere
        other than where it was, so a caller can repaint and announce only when
        something actually happened. */
    bool moveTo (juce::Point<int> to, juce::Rectangle<int> limits)
    {
        if (limits.isEmpty())
            return false;

        const auto clamped = juce::Point<int> (
            juce::jlimit (limits.getX(), limits.getRight() - 1, to.x),
            juce::jlimit (limits.getY(), limits.getBottom() - 1, to.y));

        if (placed && clamped == position)
            return false;

        position = clamped;
        placed = true;
        return true;
    }

    /** Steps by `delta`. The FIRST move places it at the limits' origin instead
        of stepping from a position it never had - so the first arrow press on a
        canvas nobody has touched puts the cursor at the beginning rather than
        one step past it. */
    bool moveBy (juce::Point<int> delta, juce::Rectangle<int> limits)
    {
        if (! placed)
            return moveTo (limits.getPosition(), limits);

        return moveTo (position + delta, limits);
    }

    void clear() noexcept
    {
        placed = false;
    }

private:
    juce::Point<int> position;
    bool placed = false;
};

} // namespace dew
