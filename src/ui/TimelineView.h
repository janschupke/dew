#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** How a horizontal step range maps onto pixels.

    The step grid, the piano roll and the playlist each used to divide their own
    width by the number of steps, which made every one of them a fixed-size view
    that could neither scroll nor zoom, and made a long pattern unreadable.
    They share this instead.

    Coordinates are relative to the *content area* - the part right of the
    keyboard or track headers - so this type knows nothing about gutters and
    stays a pure mapping.
*/
struct TimelineView
{
    double pixelsPerStep = 24.0;
    double scrollOffsetSteps = 0.0;

    /** The zoom range, shared by all four canvases.

        The floor was 3.0, which caps a 16-steps-per-bar view at 48 pixels to
        the bar - not enough of an arrangement to navigate one, and the reason
        the piano roll felt like it stopped zooming out early. It can be this
        low now because ui/GridDensity.h thins the grid and the ruler's numbers
        as the bars close up; before that, everything below about 3.0 was a
        wall of divider colour with a number every few pixels.
    */
    static constexpr double minPixelsPerStep = 1.0;
    static constexpr double maxPixelsPerStep = 120.0;

    float xForStep (double step) const noexcept
    {
        return (float) ((step - scrollOffsetSteps) * pixelsPerStep);
    }

    double stepForX (float x) const noexcept
    {
        return scrollOffsetSteps + (double) x / pixelsPerStep;
    }

    /** The step a click at this x lands in: floored, never negative. */
    int stepAtX (float x) const noexcept
    {
        return juce::jmax (0, (int) std::floor (stepForX (x)));
    }

    double visibleSteps (float contentWidth) const noexcept
    {
        return (double) contentWidth / pixelsPerStep;
    }

    /** A distance in pixels, as a distance along this timeline.

        The wheel measures in pixels, because that is the only unit the piano
        roll, the playlist, the channel rack and the score tab share. Here is
        where it becomes steps - or bars, in the playlist's instance, which
        counts those instead and never says so anywhere else either.
    */
    double stepsForPixels (double pixels) const noexcept
    {
        return pixels / pixelsPerStep;
    }

    /** First and last step touching the content area, clamped to [0, totalSteps).
        Painting walks this rather than every step, so zooming out over a long
        pattern does not cost anything.
    */
    juce::Range<int> visibleStepRange (float contentWidth, int totalSteps) const noexcept
    {
        const auto first = juce::jlimit (0, juce::jmax (0, totalSteps - 1),
                                         (int) std::floor (scrollOffsetSteps));
        const auto last = juce::jlimit (first, totalSteps,
                                        (int) std::ceil (stepForX (contentWidth)) + 1);

        return { first, last };
    }

    /** Every step touching the content area, INCLUDING past the end of the
        material.

        The clamped overload above is for walking notes: there is nothing to
        draw past the last step. This one is for drawing the grid itself, which
        has to reach the edge of the window - stopping it at the pattern end is
        what left a bare rectangle wherever the view was wider than the music.
    */
    juce::Range<int> visibleStepRange (float contentWidth) const noexcept
    {
        const auto first = juce::jmax (0, (int) std::floor (scrollOffsetSteps));
        const auto last = juce::jmax (first, (int) std::ceil (stepForX (contentWidth)) + 1);

        return { first, last };
    }

    /** Zooms by a factor while keeping whatever step is under anchorX put -
        otherwise zooming walks the music out from under the pointer.
    */
    void zoomAround (double factor, float anchorX) noexcept
    {
        const auto stepUnderAnchor = stepForX (anchorX);
        const auto wanted = juce::jlimit (minPixelsPerStep, maxPixelsPerStep,
                                          pixelsPerStep * factor);

        if (juce::exactlyEqual (wanted, pixelsPerStep))
            return;

        pixelsPerStep = wanted;
        scrollOffsetSteps = stepUnderAnchor - (double) anchorX / pixelsPerStep;
    }

    /** Keeps the view inside the material. Scrolling past the end is allowed by
        one screen minus a step, so the last bar is reachable without the view
        springing back, but a view wider than the material pins to zero.
    */
    void clampScroll (float contentWidth, int totalSteps) noexcept
    {
        const auto maxOffset = juce::jmax (0.0, (double) totalSteps - visibleSteps (contentWidth));
        scrollOffsetSteps = juce::jlimit (0.0, maxOffset, scrollOffsetSteps);
    }

    /** Scrolls the least amount that brings this step into view - used to follow
        the playhead without yanking the view on every tick.
    */
    void ensureVisible (double step, float contentWidth, double marginSteps = 1.0) noexcept
    {
        const auto visible = visibleSteps (contentWidth);

        if (step < scrollOffsetSteps + marginSteps)
            scrollOffsetSteps = juce::jmax (0.0, step - marginSteps);
        else if (step > scrollOffsetSteps + visible - marginSteps)
            scrollOffsetSteps = step - visible + marginSteps;
    }

    /** Sets the zoom so that this many steps exactly fill the content area. */
    void fit (int totalSteps, float contentWidth) noexcept
    {
        if (totalSteps <= 0 || contentWidth <= 0.0f)
            return;

        pixelsPerStep = juce::jlimit (minPixelsPerStep, maxPixelsPerStep,
                                      (double) contentWidth / (double) totalSteps);
        scrollOffsetSteps = 0.0;
    }
};

} // namespace dew
