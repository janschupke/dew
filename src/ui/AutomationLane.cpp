#include "ui/AutomationLane.h"

#include <cmath>

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"

namespace dew::automationLane
{

using namespace tokens;

namespace
{

/** How finely a segment is sampled: one point per pixel of its own width.

    Bounded by the clip, and the playlist only repaints on an edit - the playhead
    has its own strip - so this is not a per-frame cost.
*/
int samplesFor (float widthPx) noexcept
{
    return juce::jmax (2, (int) std::ceil (widthPx));
}

} // namespace

juce::Point<float> Geometry::positionOf (double step, double value) const noexcept
{
    const auto span = juce::jmax (1.0, stepSpan.getLength());
    const auto t = juce::jlimit (0.0, 1.0, (step - stepSpan.getStart()) / span);

    return { bounds.getX() + (float) t * bounds.getWidth(),
             bounds.getBottom() - (float) juce::jlimit (0.0, 1.0, value) * bounds.getHeight() };
}

double Geometry::stepAt (float x) const noexcept
{
    if (bounds.getWidth() <= 0.0f)
        return stepSpan.getStart();

    const auto t = juce::jlimit (0.0, 1.0, (double) ((x - bounds.getX()) / bounds.getWidth()));

    return stepSpan.getStart() + t * juce::jmax (1.0, stepSpan.getLength());
}

double Geometry::valueAt (float y) const noexcept
{
    if (bounds.getHeight() <= 0.0f)
        return 0.0;

    return juce::jlimit (0.0, 1.0, (double) ((bounds.getBottom() - y) / bounds.getHeight()));
}

Geometry geometryFor (juce::Rectangle<float> clipBounds, int lengthBars, int stepsPerBar) noexcept
{
    // The inset is stated HERE and nowhere else. It used to be written twice -
    // once in the position of a point and once in the inverse - and a change to
    // either broke the round trip silently.
    const auto bounds = clipBounds.reduced (2.0f, 3.0f);
    const auto steps = (double) juce::jmax (1, lengthBars * juce::jmax (1, stepsPerBar));

    return { bounds, { 0.0, steps } };
}

Hit hitTest (const Geometry& geometry, const juce::Array<juce::ValueTree>& points,
             juce::Point<float> position)
{
    if (! geometry.isEditable() || points.size() < 2)
        return {};

    // A point first: it is the smaller, more precise target, and it sits inside
    // the band its own segments occupy.
    Hit best;
    auto closest = pointGrabRadius;

    for (int i = 0; i < points.size(); ++i)
    {
        const auto at = geometry.positionOf ((double) points[i][ids::step],
                                             (double) points[i][ids::value]);
        const auto distance = at.getDistanceFrom (position);

        if (distance <= closest)
        {
            closest = distance;
            best = { Hit::Kind::point, points[i], i };
        }
    }

    if (best.kind != Hit::Kind::none)
        return best;

    // Then the drawn curve. Measured against the SAMPLES rather than the chord,
    // so a bent segment is grabbable where it actually is.
    const auto model = curvePointsOf (points.getFirst().getParent());

    if ((int) model.size() != points.size())
        return {};

    for (int i = 0; i + 1 < points.size(); ++i)
    {
        auto near = false;

        sampleSegment (geometry, model, i, [&near, position] (juce::Point<float> at)
        {
            near = near || at.getDistanceFrom (position) <= segmentGrabRadius;
        });

        if (near)
            return { Hit::Kind::segment, points[i], i };
    }

    return {};
}

bool isBendable (const juce::ValueTree& leftPoint)
{
    // A stepped segment is still a segment - it can be right-clicked, and it has
    // to be, or the shape that made it stepped could never be undone. It just
    // does not answer to a vertical drag: nothing about a step responds to one,
    // and a gesture that silently changed a shape somebody picked from a menu is
    // the worse surprise.
    return segmentShapeFromString (leftPoint[ids::shape].toString()) != SegmentShape::step;
}

void sampleSegment (const Geometry& geometry, const std::vector<CurvePoint>& points, int leftIndex,
                    const std::function<void (juce::Point<float>)>& emit)
{
    if (leftIndex < 0 || leftIndex + 1 >= (int) points.size())
        return;

    const auto& left = points[(size_t) leftIndex];
    const auto& right = points[(size_t) leftIndex + 1];

    const auto from = geometry.positionOf (left.step, left.value);
    const auto to = geometry.positionOf (right.step, right.value);
    const auto columns = samplesFor (to.x - from.x);

    for (int i = 0; i < columns; ++i)
    {
        const auto t = (double) i / (double) columns;
        const auto step = left.step + (right.step - left.step) * t;

        emit ({ from.x + (to.x - from.x) * (float) t,
                geometry.positionOf (step, curveValueAt (points, step)).y });
    }
}

void paintCurve (juce::Graphics& g, const Geometry& geometry,
                 const juce::Array<juce::ValueTree>& points, const Style& style)
{
    if (points.size() < 2)
        return;

    const auto model = curvePointsOf (points.getFirst().getParent());

    if ((int) model.size() != points.size())
        return;

    // A centre line for pan-like targets. AutomationTarget has always carried
    // `bipolar` and its own comment says a point editor centres it; nothing ever
    // read it.
    if (style.bipolar)
    {
        const auto centre = geometry.positionOf (geometry.stepSpan.getStart(), 0.5).y;

        g.setColour (style.curve.withAlpha (emphasis::wash));
        g.fillRect (geometry.bounds.getX(), centre - stroke::whisper * 0.5f,
                    geometry.bounds.getWidth(), stroke::whisper);
    }

    for (int i = 0; i + 1 < points.size(); ++i)
    {
        juce::Path segment;
        auto started = false;

        sampleSegment (geometry, model, i, [&segment, &started] (juce::Point<float> at)
        {
            if (std::exchange (started, true))
                segment.lineTo (at);
            else
                segment.startNewSubPath (at);
        });

        if (! started)
            continue;

        // The right endpoint EXACTLY, which sampling deliberately never reaches.
        // It is what puts the last pixel of every shape where its handle is, and
        // what makes a stepped segment jump rather than lean.
        segment.lineTo (geometry.positionOf (model[(size_t) i + 1].step,
                                             model[(size_t) i + 1].value));

        const auto hovered = style.hoveredSegment == i;

        g.setColour (style.curve);
        g.strokePath (segment, juce::PathStrokeType (hovered ? stroke::bold : stroke::regular));
    }

    for (const auto& point : points)
    {
        const auto at = geometry.positionOf ((double) point[ids::step], (double) point[ids::value]);

        g.setColour (colour::wellDeep);
        g.fillEllipse (juce::Rectangle<float> (pointDotOuter, pointDotOuter).withCentre (at));
        g.setColour (style.curve);
        g.fillEllipse (juce::Rectangle<float> (pointDotInner, pointDotInner).withCentre (at));
    }
}

} // namespace dew::automationLane
