#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/AutomationCurve.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/AutomationLane.h"

using namespace dew;
using namespace dew::automationLane;
using Catch::Matchers::WithinAbs;

namespace
{

/** A curve on a project, so the points are real ValueTree children with a
    parent - hitTest reads the automation through them.
*/
struct Curve
{
    Curve (std::initializer_list<std::pair<double, double>> points)
    {
        project = ProjectFactory::createDefault();

        for (const auto& target : availableAutomationTargets (project))
            if (target.displayName.endsWith ("> Volume"))
            {
                automation = ProjectEdits::addAutomation (project, target, &undo);
                break;
            }

        REQUIRE (automation.isValid());

        for (auto point : ProjectEdits::sortedAutomationPoints (automation))
            automation.removeChild (automation.indexOf (point), nullptr);

        for (const auto& [step, value] : points)
        {
            auto point = ProjectEdits::addAutomationPoint (automation, step, value, nullptr);
            REQUIRE (point.isValid());
        }
    }

    juce::Array<juce::ValueTree> points() const
    {
        return ProjectEdits::sortedAutomationPoints (automation);
    }

    juce::ValueTree project, automation;
    juce::UndoManager undo;
};

/** A lane 400px wide and 120 tall, spanning 16 steps - roughly what one bar of a
    tall track looks like. */
Geometry laneOf (float width = 400.0f, float height = 120.0f)
{
    return geometryFor ({ 100.0f, 200.0f, width, height }, 1, 16);
}

} // namespace

TEST_CASE ("a lane's position and its inverse agree", "[ui][automationLane]")
{
    // No component at all: a Geometry in, a step and a value out. The inset used
    // to be written twice - in the position of a point and in the inverse - and
    // a change to either broke this round trip silently.
    const auto lane = laneOf();

    for (int i = 0; i <= 20; ++i)
    {
        const auto step = 16.0 * (double) i / 20.0;

        for (int j = 0; j <= 10; ++j)
        {
            const auto value = (double) j / 10.0;
            const auto at = lane.positionOf (step, value);

            INFO ("step " << step << " value " << value);
            REQUIRE_THAT (lane.stepAt (at.x), WithinAbs (step, 0.05));
            REQUIRE_THAT (lane.valueAt (at.y), WithinAbs (value, 0.01));
        }
    }
}

TEST_CASE ("a lane picks the nearer of two points", "[ui][automationLane]")
{
    const Curve curve { { 0.0, 0.5 }, { 8.0, 0.5 }, { 16.0, 0.5 } };
    const auto lane = laneOf();
    const auto points = curve.points();

    const auto middle = lane.positionOf (8.0, 0.5);
    const auto hit = hitTest (lane, points, middle.translated (2.0f, 0.0f));

    REQUIRE (hit.kind == Hit::Kind::point);
    REQUIRE (hit.index == 1);
}

TEST_CASE ("a point wins over the segment it sits on", "[ui][automationLane]")
{
    // Both are under the pointer at a point: the point is the smaller, more
    // precise target, and segmentGrabRadius is deliberately the narrower of the
    // two so the overlap always resolves this way.
    const Curve curve { { 0.0, 0.2 }, { 16.0, 0.8 } };
    const auto lane = laneOf();

    const auto hit = hitTest (lane, curve.points(), lane.positionOf (0.0, 0.2));

    REQUIRE (hit.kind == Hit::Kind::point);
    REQUIRE (hit.index == 0);
}

TEST_CASE ("a bent segment is grabbable where it is drawn, not on its chord",
           "[ui][automationLane]")
{
    // The property that makes the bend gesture honest. Measuring against the
    // chord would put the grab zone somewhere the curve visibly is not.
    Curve curve { { 0.0, 0.0 }, { 16.0, 1.0 } };
    const auto lane = laneOf();

    auto first = curve.points().getFirst();
    first.setProperty (ids::curve, 0.9, nullptr);

    const auto model = curvePointsOf (curve.automation);
    const auto midStep = 8.0;

    const auto onCurve = lane.positionOf (midStep, curveValueAt (model, midStep));
    const auto onChord = lane.positionOf (midStep, 0.5);

    // The bend has actually moved the curve away from the chord, or this test
    // would pass for the wrong reason.
    INFO ("curve " << onCurve.y << " chord " << onChord.y);
    REQUIRE (std::abs (onCurve.y - onChord.y) > segmentGrabRadius * 2.0f);

    REQUIRE (hitTest (lane, curve.points(), onCurve).kind == Hit::Kind::segment);
    REQUIRE (hitTest (lane, curve.points(), onChord).kind == Hit::Kind::none);
}

TEST_CASE ("a stepped segment refuses the bend", "[ui][automationLane]")
{
    Curve curve { { 0.0, 0.2 }, { 16.0, 0.8 } };
    const auto lane = laneOf();

    auto first = curve.points().getFirst();
    ProjectEdits::setPointShape (first, SegmentShape::step, nullptr);

    // It is still FOUND - a stepped segment has to be right-clickable, or the
    // shape that made it stepped could never be undone - and it is still drawn
    // where the hit test looks for it, along the flat run at the left value.
    const auto onFlat = lane.positionOf (8.0, 0.2);
    const auto hit = hitTest (lane, curve.points(), onFlat);

    REQUIRE (hit.kind == Hit::Kind::segment);

    // It just does not answer to a vertical drag.
    REQUIRE_FALSE (isBendable (hit.point));
    REQUIRE (isBendable (curve.points()[1]));
}

TEST_CASE ("a lane too narrow to edit reports nothing", "[ui][automationLane]")
{
    // Three targets - a point, a segment and the clip's own right edge - inside a
    // few pixels are three targets fighting, not three targets.
    const Curve curve { { 0.0, 0.5 }, { 16.0, 0.5 } };
    const auto lane = geometryFor ({ 100.0f, 200.0f, 8.0f, 120.0f }, 1, 16);

    REQUIRE_FALSE (lane.isEditable());
    REQUIRE (hitTest (lane, curve.points(), lane.positionOf (0.0, 0.5)).kind == Hit::Kind::none);
}

TEST_CASE ("what the lane draws is what the evaluator says", "[ui][automationLane]")
{
    // The strongest form of the drawn-equals-heard check, with no pixels in it:
    // every sample the painter would emit is compared against the value the
    // audio thread would read at the same step.
    Curve curve { { 0.0, 0.1 }, { 6.0, 0.9 }, { 16.0, 0.3 } };
    const auto lane = laneOf();

    auto points = curve.points();
    points.getFirst().setProperty (ids::curve, -0.7, nullptr);
    ProjectEdits::setPointShape (points[1], SegmentShape::step, nullptr);

    const auto model = curvePointsOf (curve.automation);
    int sampled = 0;

    for (int i = 0; i + 1 < (int) model.size(); ++i)
        sampleSegment (lane, model, i,
                       [&] (juce::Point<float> at)
                       {
                           ++sampled;

                           const auto step = lane.stepAt (at.x);

                           INFO ("segment " << i << " at step " << step);
                           REQUIRE_THAT ((double) lane.valueAt (at.y),
                                         WithinAbs (curveValueAt (model, step), 0.02));
                       });

    // A control case: a comparison over no samples is not a comparison.
    REQUIRE (sampled > 100);
}
