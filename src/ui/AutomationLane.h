#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AutomationCurve.h"

namespace dew::automationLane
{

/** Editing an automation curve inside a playlist clip.

    A namespace of value types and free functions rather than a juce::Component,
    for three reasons that all come from where this lives:

      - it has no bounds of its own. It is a rectangle inside a row that the
        playlist's paint loop already walks, and a child component per clip would
        be rebuilt on every tree change and re-parented mid-drag whenever a clip
        crossed a track;
      - a child would swallow the clip gesture. A plain press inside an
        automation clip that is not on the curve still MOVES the clip, and there
        is a test that says so; punching a hole in a hitTest is worse than the
        branch order the playlist's mouseDown already has;
      - free functions test with no component at all - a Geometry in, a step and
        a value out. Given how many ways a headless UI test can pass while
        exercising nothing here, that is the strongest test available, and it is
        the only way "what is drawn is what is heard" becomes directly
        assertable.

    ruler::Gesture, timelinePaint and paint::waveform are the precedent; dew has
    none for a component per object.
*/

/** How near a point a press has to be to grab it. */
inline constexpr float pointGrabRadius = 7.0f;

/** How near the DRAWN curve a press has to be to mean "bend this segment"
    rather than "move this clip".

    Smaller than pointGrabRadius deliberately: a point wins wherever the two
    overlap, and everything further out than this is still the clip - so no
    gesture is taken away, which is what makes the bend safe to add to a press
    that already meant something.
*/
inline constexpr float segmentGrabRadius = 4.0f;

/** Below this a clip is too narrow to edit and hitTest reports nothing.

    Three targets - a point, a segment and the clip's own right edge - inside a
    rectangle a few pixels wide are three targets fighting, not three targets.
    The piano roll drops its step lines below a similar width for the same
    reason.
*/
inline constexpr float minEditableWidth = 16.0f;

/** The dots a point is drawn as: a hole in the clip's fill and a filled centre. */
inline constexpr float pointDotOuter = 7.0f;
inline constexpr float pointDotInner = 5.0f;

/** Where a curve sits on screen, and what its horizontal axis means.

    Coordinates are the HOST's, so this knows nothing about gutters or rows and
    stays a pure mapping - the rule TimelineView already follows.
*/
struct Geometry
{
    juce::Rectangle<float> bounds; ///< already inset
    juce::Range<double> stepSpan { 0.0, 16.0 };

    juce::Point<float> positionOf (double step, double value) const noexcept;

    double stepAt (float x) const noexcept;
    double valueAt (float y) const noexcept;

    bool isEditable() const noexcept
    {
        return bounds.getWidth() >= minEditableWidth;
    }
};

/** The geometry for one clip.

    `stepSpan` starts at zero because a clip today shows a curve NORMALISED to
    its own length, while the engine reads the curve at absolute steps from the
    clip's start. The two disagree the moment a clip is resized, which is a
    separate decision about what a clip's length means for a shared automation -
    the span is a Range rather than a length so that fixing it later is a changed
    constructor and not a changed call site.
*/
Geometry geometryFor (juce::Rectangle<float> clipBounds, int lengthSteps) noexcept;

/** What is under a position: a point, the segment to a point's right, or
    nothing. `point` is the point itself, or the segment's LEFT point.
*/
struct Hit
{
    enum class Kind
    {
        none,
        point,
        segment
    };

    Kind kind = Kind::none;
    juce::ValueTree point;
    int index = -1;
};

Hit hitTest (const Geometry&, const juce::Array<juce::ValueTree>& points, juce::Point<float>);

/** Whether the segment a point owns answers to a bend drag.

    Separate from hitTest on purpose. A stepped segment is still a segment and
    has to be right-clickable, or the shape that made it stepped could never be
    undone; it simply does not respond to a vertical drag. Reporting it as
    nothing would have been the tidier hit test and the worse editor.
*/
bool isBendable (const juce::ValueTree& leftPoint);

/** The DRAWN shape of one segment, sampled left to right.

    Emits t in [0, 1) - never the right endpoint - so the caller always finishes
    with a lineTo to where that point's handle actually is. That is what gives
    every shape an exact endpoint, and what makes a stepped segment jump rather
    than lean.

    The painter walks this and the hit test measures against it, so what you can
    grab is what you can see.
*/
void sampleSegment (const Geometry&, const std::vector<CurvePoint>& points, int leftIndex,
                    const std::function<void (juce::Point<float>)>& emit);

struct Style
{
    juce::Colour curve;
    bool bipolar = false;
    int hoveredSegment = -1; ///< index of the segment's LEFT point, or -1
};

void paintCurve (juce::Graphics&, const Geometry&, const juce::Array<juce::ValueTree>& points,
                 const Style&);

} // namespace dew::automationLane
