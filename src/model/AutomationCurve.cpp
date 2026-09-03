#include "model/AutomationCurve.h"

#include <cmath>

#include "model/Ids.h"

namespace dew
{

SegmentShape segmentShapeFromString (const juce::String& text)
{
    if (text == "step")
        return SegmentShape::step;

    return SegmentShape::curve;
}

juce::String segmentShapeToString (SegmentShape shape)
{
    switch (shape)
    {
        case SegmentShape::step: return "step";
        case SegmentShape::curve: break;
    }

    return "curve";
}

double curveValueAt (juce::Span<const CurvePoint> points, double step) noexcept
{
    if (points.empty())
        return 0.0;

    if (step <= points.front().step)
        return points.front().value;

    if (step >= points.back().step)
        return points.back().value;

    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& right = points[i];

        if (step > right.step)
            continue;

        const auto& left = points[i - 1];
        const auto span = right.step - left.step;

        // Two points on one step have no defined value between them; the right
        // one wins, which is the same rule as landing exactly on it.
        if (span <= 0.0)
            return right.value;

        if (left.shape == SegmentShape::step)
            return left.value;

        auto t = (step - left.step) / span;

        // The bend moves the interpolation without moving either endpoint, so a
        // shape can be eased without adding points to fake it.
        const auto bend = juce::jlimit (-1.0, 1.0, left.curve);

        if (! juce::approximatelyEqual (bend, 0.0))
            t = std::pow (t, std::pow (2.0, -bend * 2.0));

        return left.value + (right.value - left.value) * t;
    }

    return points.back().value;
}

std::vector<CurvePoint> curvePointsOf (const juce::ValueTree& automation)
{
    std::vector<CurvePoint> points;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            points.push_back ({ (double) child[ids::step],
                                juce::jlimit (0.0, 1.0, (double) child[ids::value]),
                                juce::jlimit (-1.0, 1.0, (double) child[ids::curve]),
                                segmentShapeFromString (child[ids::shape].toString()) });

    return points;
}

} // namespace dew
