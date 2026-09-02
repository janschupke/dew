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

    /** Beats to the bar - the project's meter numerator. Carried alongside
        stepsPerBar rather than derived from it: the ruler used to recover the
        beat as `stepsPerBar / 4`, which is silently wrong in every meter but
        4/4 and put beat ticks on non-beats in all three editors at once.
    */
    int beatsPerBar = 4;

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

/** What a ruler gesture needs to know that is not in the TimelineView.

    Units are whatever the host's TimelineView counts: steps in the piano roll
    and the channel rack, bars in the playlist. The gesture never converts
    between the two, so it cannot get the conversion wrong - whoever owns the
    timeline owns its unit as well.
*/
struct GestureContext
{
    /** One quantisation unit: a beat where the timeline counts steps, a bar
        where it counts bars. A loop is a musical span, so a drag lands on one
        of these rather than wherever the pointer happened to be.
    */
    int snapUnits = 1;

    /** Units in the material, so a span cannot run off the end of it. */
    int totalUnits = 16;

    /** Where the transport is, in the same units. */
    double playheadUnits = 0.0;
};

/** The press, drag, release and double-click a ruler answers to.

    Three views wanted this and each went its own way: the piano roll and the
    playlist wrote the same six behaviours twice, against two private Gesture
    enums and two anchor fields, and the channel rack's ruler had none of them
    and could only seek. This owns them once. It is driven through callbacks
    rather than inherited from, so a host that already has its own layout and
    its own unit keeps both.

    Every handler returns true when it took the event, so a host can keep its
    own branches for everything below the ruler.

        plain press     scrub, and keep scrubbing for the drag
        shift press     anchor here, drag either way; released without moving,
                        the span is taken back
        mod press       loop from the playhead to here - and if that press
                        becomes a drag, the span restarts at the press rather
                        than keeping an anchor nobody chose
        double-click    take the span back
*/
class Gesture
{
public:
    std::function<GestureContext()> context;

    /** The unsnapped position an x means, clamped to the material. */
    std::function<double (int x)> unitForX;

    std::function<void (double units)> onSeek;
    std::function<void (juce::Range<int> units)> onRangeChanged;
    std::function<void()> onRangeCleared;

    bool mouseDown (const juce::MouseEvent&);
    bool mouseDrag (const juce::MouseEvent&);
    bool mouseUp (const juce::MouseEvent&);
    bool mouseDoubleClick (const juce::MouseEvent&);

    /** True between a press this took and its release, so a host can tell a
        ruler drag from one of its own without keeping a second flag.
    */
    bool isActive() const noexcept { return mode != Mode::none; }

private:
    enum class Mode { none, scrubbing, selecting };

    GestureContext contextOrDefault() const;
    double unitAt (int x) const;
    int snapUnit (double raw, bool roundUp) const;

    /** Emits the span between two unsnapped positions, snapped OUTWARDS at
        both ends and widened to one unit if it collapsed.

        Outwards rather than by drag direction: rounding the near end towards
        the far one hands back less than was asked for, and doing it by
        direction dropped the unit a backwards drag started on. A drag that has
        not yet crossed a line still means one unit, not none - otherwise the
        strip flickers in and out at the start of every gesture.
    */
    void applySpan (double from, double to);

    Mode mode = Mode::none;

    /** Where the press was, unsnapped, so a drag can be measured from it and,
        after a mod-press, re-anchored to it.
    */
    double anchorRaw = 0.0;

    /** Whether a press that never moves takes the span back. A shift-click
        does; a mod-click has already said what it wants.
    */
    bool clearsIfUnmoved = false;
};

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

    /** The span a shift-drag or a mod-click asks for, in steps, and the way it
        is taken back. Left unset, the strip only seeks - which is all it could
        do before, and why the sequencer's ruler was the one place in the app a
        loop could not be chosen.
    */
    std::function<void (juce::Range<int> steps)> onRangeChanged;
    std::function<void()> onRangeCleared;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    /** Follows the transport, so the head moves. Repaints only when the
        position actually changed - a ruler is mostly static and has no
        business redrawing sixty times a second while nothing is happening.
    */
    void timerCallback() override;

    /** Binds the gesture's callbacks the first time one is needed. The sources
        it reads through are set by the host after construction, so a strip that
        captured them in its constructor would act on a timeline it no longer has.
    */
    void prepareGesture();

    ruler::Gesture gesture;

    TimelineView fallback;
    double lastPaintedPlayhead = -1.0;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RulerStrip)
};

} // namespace dew
