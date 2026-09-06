#pragma once

#include <juce_core/juce_core.h>

namespace dew::gridDensity
{

/** How far apart a timeline's marks have to be before they stop being a grid.

    dew's four canvases all draw the same thing - a line per bar, per beat, per
    snap cell, per step - and until this existed only the two FINEST of those
    tiers ever thinned out. A bar line and a beat line were drawn at every bar
    and every beat however far out the view was zoomed, so at the bottom of the
    zoom range the playlist drew a bar line, and a bar NUMBER, every three
    pixels: the grid stopped being readable exactly where a grid is the only
    thing telling you where you are.

    The answer is a stride rather than a cutoff. A cutoff is what the ruler's
    bar numbers had - `>= 28.0`, and below it every number in the window
    disappeared at once - which trades a wall of digits for no digits at all
    and never gives the reader the useful middle. A stride keeps the marks and
    spends the ones in between: 1, 2, 3, 4 becomes 1, 3, 5 becomes 1, 5, 9.
*/

/** The smallest stride at which marks this far apart clear `minimumSpacingPx`.

    POWERS OF TWO, and that is the property worth having: every stride's marks
    are a subset of the one below it, so zooming out only ever REMOVES lines.
    Any other progression - 1, 2, 3, 5 - moves them, and a bar line that slides
    sideways as you zoom is worse than one that goes away.

    Returns 1 whenever the marks already clear the spacing, so the ordinary
    case costs a comparison and callers need no branch of their own.
*/
inline int strideFor (double pixelsPerUnit, double minimumSpacingPx) noexcept
{
    if (! (pixelsPerUnit > 0.0) || pixelsPerUnit >= minimumSpacingPx)
        return 1;

    // A view zoomed absurdly far out is still a view, and a loop that trusts
    // the arithmetic to terminate is one denormal away from not doing.
    constexpr int widest = 1 << 16;

    auto stride = 1;

    while (stride < widest && (double) stride * pixelsPerUnit < minimumSpacingPx)
        stride *= 2;

    return stride;
}

/** The spacings the strides are measured against.

    One vocabulary rather than three, because the ruler and the grid under it
    have to agree: a number over a bar the grid did not draw a line for is
    worse than either of them thinning alone.
*/

/** A bar line is the coarsest structure on the canvas and can stand closer
    together than anything else before it reads as fill. */
inline constexpr double barSpacingPx = 24.0;

/** A beat is a subdivision, so it needs the room to read as one. The number
    the ruler's beat ticks already used. */
inline constexpr double beatSpacingPx = 10.0;

/** Room for two digits and the gap after them. What the bar numbers' old
    all-or-nothing cutoff was, kept - it was the right distance, and only
    the wrong thing to do with it. */
inline constexpr double labelSpacingPx = 28.0;

} // namespace dew::gridDensity
