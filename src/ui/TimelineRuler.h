#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TimelineView.h"

namespace dew
{

/** The bar-numbered strip above a timeline, and the click that moves the
    playhead along it.

    Three views wanted one of these and no two of them had the same thing: the
    piano roll hand-rolled a ruler, the playlist hand-rolled a different one,
    and the channel rack had none at all. None of the three responded to a
    click. Shared code rather than a shared base class, so a component that
    already owns its layout can keep it and still draw the same ruler as the
    others.
*/
namespace ruler
{

/** What a ruler needs to know that is not in the TimelineView. */
struct Style
{
    int stepsPerBar = 16;

    /** Steps in the material. Bars past this are drawn dimmed rather than
        omitted, matching how the grids below them behave.
    */
    int totalSteps = 16;

    /** Position to mark, in steps, or a negative number for none. */
    double playheadSteps = -1.0;

    /** A stopped transport still shows its position, at half strength. */
    bool playing = false;

    /** The selected span, in the same units as everything else here, or a
        negative start for none. Drawn as a solid strip rather than a wash: a
        marker whose whole job is saying what will play cannot be subtle, and the
        first attempt at this in the playlist - a faint tint across the full
        height - was too hard to find to be worth anything.
    */
    double selectionStartSteps = -1.0;
    double selectionEndSteps = -1.0;

    bool hasSelection() const noexcept
    {
        return selectionStartSteps >= 0.0 && selectionEndSteps > selectionStartSteps;
    }
};

/** Draws the ruler into `bounds`. Step 0 is at bounds.getX(). */
void paint (juce::Graphics&, juce::Rectangle<int> bounds, const TimelineView&, const Style&);

/** The position, in steps, that a click at this x means.

    Clamped to the material: dragging off the end of a short pattern should
    park at its end rather than seeking into empty space.
*/
double stepForClick (int x, juce::Rectangle<int> bounds, const TimelineView&, int totalSteps);

} // namespace ruler

/** A ruler as an actual component.

    For the channel rack, whose grid lives inside a vertically scrolling
    Viewport - a ruler drawn inside the grid would scroll away with it. The
    piano roll and the playlist draw theirs inline, because they already own
    the whole of their own bounds.
*/
class RulerStrip : public juce::Component,
                   private juce::Timer
{
public:
    RulerStrip();
    ~RulerStrip() override;

    /** Called for every paint, so the strip never holds a stale copy of a
        timeline it does not own.
    */
    std::function<const TimelineView&()> timelineSource;
    std::function<ruler::Style()> styleSource;

    /** Fired on a press and on every drag along the strip. */
    std::function<void (double steps)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    /** Follows the transport, so the head moves. Repaints only when the
        position actually changed - a ruler is mostly static and has no
        business redrawing sixty times a second while nothing is happening.
    */
    void timerCallback() override;

    void seekTo (const juce::MouseEvent&);

    TimelineView fallback;
    double lastPaintedPlayhead = -1.0;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RulerStrip)
};

} // namespace dew
