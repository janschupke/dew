#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** How a stack of equal-height rows maps onto pixels.

    TimelineView is the same idea for the horizontal axis, and the two editors
    that have both axes shared it from the start. The vertical one they wrote
    twice: the piano roll's pitch rows and the playlist's lanes each had their
    own scroll offset, their own clamp, their own anchor-preserving setter and
    their own multiply-or-fit, in bodies that matched line for line - down to
    the comment, which in both files said "for the same reason
    TimelineView::zoomAround anchors on the pointer".

    Two copies of an arithmetic this fiddly is one bug waiting to be fixed in
    one of them. They share this instead.

    Coordinates are relative to the *content area* - below the ruler, right of
    the keyboard or the track headers - so this type knows nothing about
    gutters and stays a pure mapping, exactly as TimelineView does.

    The clamps are members rather than constants because the two editors do not
    agree on them: a pitch row is 8..40 and a lane is a row..six rows.
*/
struct RowView
{
    int height = 14;
    double scrollPx = 0.0;

    int minHeight = 1;
    int maxHeight = 1;

    /** The height before it was rounded to a pixel, or 0 meaning "the integer
        above is the truth" - the state a view starts in.

        A wheel notch is a FACTOR, and a trackpad sends that factor in
        fragments: 2^(0.002 x 3) is 1.004, which of a 34px lane asks for
        34.14px. Rounding at every event answered 34 for ever, so the one
        gesture this axis has did nothing at all on a trackpad while working
        from a mouse - and it looked like a missing feature rather than a
        rounding bug. The horizontal axis never had it, because
        TimelineView::pixelsPerStep is a double and this was not.

        Kept even when the rounded height does not move, which is the whole
        point: twenty events that each ask for a seventh of a pixel are one
        pixel and a half, not nothing.
    */
    double exactHeight = 0.0;

    /** How tall this many rows are, whether or not that fits. */
    double contentHeight (int rows) const noexcept
    {
        return (double) juce::jmax (0, rows) * (double) height;
    }

    /** The top of a row, scroll applied. Add the content area's own origin. */
    float yForRow (int index) const noexcept
    {
        return (float) ((double) index * (double) height - scrollPx);
    }

    /** The row a y lands in. Floored, and NEGATIVE above the first row.

        Unlike TimelineView::stepAtX, which clamps: a step before zero is not a
        step, but a y above the first row is a real answer meaning "not on a
        row", and both callers rely on it. std::floor rather than integer
        division for the same reason - division truncates toward zero, so a y
        one pixel above the rows answered 0 rather than -1.
    */
    int rowAtY (double y) const noexcept
    {
        if (height <= 0)
            return 0;

        return (int) std::floor ((y + scrollPx) / (double) height);
    }

    /** Grows or shrinks the rows, keeping whatever row is at the middle of the
        view where it is - otherwise growing them from the top walks the music
        out from under whatever you were looking at, which is the same reason
        TimelineView::zoomAround anchors on the pointer.

        Only when there is something to anchor TO. With the rows already
        fitting there is no scroll position to preserve, and the centre of the
        view is then a row that does not exist - four lanes in a window six
        tall put the anchor on lane six, and growing them scrolled the whole
        arrangement off the top.

        Returns whether the height actually changed, so a caller can skip the
        repaint and, in the playlist's instance, decide for itself whether the
        scroll this computed is the one it wants: a resize drag holds the
        scroll it started with, because re-centring between drag samples moves
        the grabbed edge away from the hand holding it.
    */
    bool setHeight (double wanted, double viewHeight, int rows) noexcept
    {
        // Clamped as a REAL number and remembered as one, so a zoom that runs
        // into an end stop does not have to be wound back out of a value it
        // never actually reached.
        exactHeight = juce::jlimit ((double) minHeight, (double) maxHeight, wanted);

        const auto clamped = juce::roundToInt (exactHeight);

        if (clamped == height || height <= 0)
            return false;

        const auto scrollable = contentHeight (rows) > viewHeight;
        const auto anchorRow = (scrollPx + viewHeight * 0.5) / (double) height;

        height = clamped;
        scrollPx = scrollable ? juce::jmax (0.0, anchorRow * (double) height - viewHeight * 0.5)
                              : 0.0;

        return true;
    }

    /** The height a zoom by this factor asks for, before clamping. Separate
        from setHeight because a factor of zero or less means "fit" in both
        editors, and what fitting means is theirs to say.

        In pixels and a fraction of one, not in whole pixels: it multiplies the
        height the last zoom ASKED for rather than the pixel that was drawn, so
        a hundred fragments of a notch compose into the same distance one notch
        travels. See exactHeight.
    */
    double zoomedHeight (double factor) const noexcept
    {
        return (exactHeight > 0.0 ? exactHeight : (double) height) * factor;
    }

    /** The height at which this many rows exactly fill the view.

        `fallback` is what an unlaid-out view answers, and it is a parameter
        because the two editors do not agree: the piano roll names its default
        row height, and the playlist names a height it knows setHeight will
        clamp up to the minimum. Unifying that here would change one of them
        for a case neither can reach with a window on screen, which is not
        something a move should decide.
    */
    int heightToFit (int rows, double viewHeight, int fallback) const noexcept
    {
        const auto usable = juce::jmax (1, rows);

        return viewHeight > 0.0 ? (int) (viewHeight / (double) usable) : fallback;
    }

    /** Keeps the view inside the rows. A view taller than they are pins to
        zero, so there is no scrolling into empty space below the last one.
    */
    void clampScroll (double viewHeight, int rows) noexcept
    {
        scrollPx = juce::jlimit (0.0, juce::jmax (0.0, contentHeight (rows) - viewHeight),
                                 scrollPx);
    }
};

} // namespace dew
