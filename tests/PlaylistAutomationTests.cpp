// Automation clips: the curve, its points, and the shapes between them.
//
// Split out of a PlaylistTests.cpp that was 1,788 lines. The fixture every
// one of them uses is PlaylistHarness.h.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineSnapshot.h"
#include "model/AutomationCurve.h"
#include "model/AutomationTargets.h"
#include "ui/AutomationLane.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

#include "ConfirmSupport.h"
#include "PaintProbe.h"
#include "PlaylistHarness.h"

using namespace dew;
using namespace dew::testing;
using namespace Catch::Matchers;

namespace
{

/** The volume target of the project's first channel, by whatever it is called. */
juce::String firstChannelVolumeTarget (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::CHANNEL)[ids::name].toString() + " > Volume";
}

AutomationTarget targetNamed (const juce::ValueTree& project, const juce::String& name)
{
    for (const auto& target : availableAutomationTargets (project))
        if (target.displayName == name)
            return target;

    FAIL ("no automation target named " << name);
    return {};
}

/** The screen position of an automation point, asked of the component. */
juce::Point<int> pointPositionOf (PlaylistHarness& h, const juce::ValueTree& clip, int trackIndex,
                                  const juce::ValueTree& point)
{
    return h.playlist.pointPosition (clip, trackIndex, point).toInt();
}

juce::ValueTree firstPointOf (const juce::ValueTree& automation)
{
    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
            return point;

    return {};
}

} // namespace

TEST_CASE ("choosing a target creates an automation and a clip for it",
           "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(),
                                     firstChannelVolumeTarget (h.document.getState()));
    const auto clip = h.playlist.createAutomationClip (target, 0, 4);

    REQUIRE (clip.isValid());
    REQUIRE (ProjectEdits::isAutomationClip (clip));

    const auto automation = ProjectEdits::findAutomation (h.document.getState(),
                                                          (int) clip[ids::automationId]);
    REQUIRE (automation.isValid());
    REQUIRE (automation[ids::name].toString() == firstChannelVolumeTarget (h.document.getState()));

    // On a free lane, not stacked invisibly under the clip that is already there.
    REQUIRE (h.countClips (0) == 1);
}

TEST_CASE ("a bend is drawn, not straightened", "[ui][playlist][automation]")
{
    // The painter drew a chord between every pair of points, so the bend both
    // evaluators have always honoured was HEARD and not SEEN. Two renders of the
    // same curve, bent opposite ways, used to be pixel-identical.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(),
                                     firstChannelVolumeTarget (h.document.getState()));
    const auto clip = h.playlist.createAutomationClip (target, 0, 4);
    REQUIRE (clip.isValid());

    auto automation = ProjectEdits::findAutomation (h.document.getState(),
                                                    (int) clip[ids::automationId]);
    REQUIRE (automation.isValid());

    // A rising line across the clip, so a bend has somewhere to bulge.
    juce::Array<juce::ValueTree> points;

    for (auto point : automation)
        if (point.hasType (ids::POINT))
            points.add (point);

    REQUIRE (points.size() == 2);
    points.getFirst().setProperty (ids::value, 0.0, nullptr);
    points.getLast().setProperty (ids::value, 1.0, nullptr);

    const auto renderWithBend = [&h, &points] (double bend)
    {
        points.getFirst().setProperty (ids::curve, bend, nullptr);
        return dew::testing::render (h.playlist);
    };

    const auto bentUp = renderWithBend (0.9);
    const auto bentDown = renderWithBend (-0.9);

    int changed = 0;

    for (int y = 0; y < bentUp.getHeight(); ++y)
        for (int x = 0; x < bentUp.getWidth(); ++x)
            if (bentUp.getPixelAt (x, y) != bentDown.getPixelAt (x, y))
                ++changed;

    INFO ("pixels differing between a curve bent up and the same one bent down: " << changed);
    REQUIRE (changed > 100);

    // And a control case, or the above would also pass if the painter had simply
    // become nondeterministic: the same bend twice is the same picture.
    const auto again = renderWithBend (-0.9);

    int unstable = 0;

    for (int y = 0; y < again.getHeight(); ++y)
        for (int x = 0; x < again.getWidth(); ++x)
            if (again.getPixelAt (x, y) != bentDown.getPixelAt (x, y))
                ++unstable;

    REQUIRE (unstable == 0);
}

TEST_CASE ("an automation point can be dragged, added and removed", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(),
                                     firstChannelVolumeTarget (h.document.getState()));
    const auto clip = h.playlist.createAutomationClip (target, 0, 4);
    REQUIRE (clip.isValid());

    const auto automation = ProjectEdits::findAutomation (h.document.getState(),
                                                          (int) clip[ids::automationId]);

    const auto countPoints = [&automation]
    {
        int n = 0;

        for (const auto& point : automation)
            if (point.hasType (ids::POINT))
                ++n;

        return n;
    };

    REQUIRE (countPoints() == 2);

    // Which lane it landed on; createAutomationClip picks the first free one.
    int trackIndex = -1;

    for (int i = 0; i < h.playlist.getNumTracks(); ++i)
        for (const auto& candidate : h.track (i))
            if (candidate == clip)
                trackIndex = i;

    REQUIRE (trackIndex >= 0);

    // Drag the first point downwards: its value must fall.
    auto point = firstPointOf (automation);
    const auto before = (double) point[ids::value];
    const auto from = pointPositionOf (h, clip, trackIndex, point);

    h.playlist.mouseDown (eventAt (h.playlist, from));
    h.playlist.mouseDrag (eventAt (h.playlist, { from.x, from.y + 12 }));
    h.playlist.mouseUp (eventAt (h.playlist, { from.x, from.y + 12 }));

    INFO ("value " << before << " -> " << (double) point[ids::value]);
    REQUIRE ((double) point[ids::value] < before);

    // The clip itself must not have moved: grabbing a point is not grabbing the
    // clip, or a curve could never be edited without dragging the whole thing.
    REQUIRE ((int) clip[ids::startBar] == 0);

    // Double-clicking inside adds a point.
    const auto middle = h.playlist.getBoundsForClip (clip, trackIndex).getCentre().toInt();
    h.playlist.mouseDoubleClick (eventAt (h.playlist, middle, 2));
    REQUIRE (countPoints() == 3);

    // Alt-clicking a point removes it, and does not remove the clip.
    const auto added = h.playlist.getBoundsForClip (clip, trackIndex).getCentre().toInt();
    const auto alt = juce::ModifierKeys (juce::ModifierKeys::altModifier);

    h.playlist.mouseDown (eventAt (h.playlist, added, 1, alt));
    h.playlist.mouseUp (eventAt (h.playlist, added, 1, alt));

    REQUIRE (countPoints() == 2);
    REQUIRE (ProjectEdits::findAutomation (h.document.getState(), (int) clip[ids::automationId])
                 .isValid());
}

namespace
{

/** An automation clip on track 0, with a rising line across it, and the
    playlist tall enough that the curve has a value axis worth aiming at.
*/
struct CurveHarness
{
    explicit CurveHarness (PlaylistHarness& harness)
        : h (harness)
    {
        h.playlist.setTrackHeight (tokens::size::trackHeightMax);

        const auto target = targetNamed (h.document.getState(),
                                         firstChannelVolumeTarget (h.document.getState()));
        clip = h.playlist.createAutomationClip (target, 0, 4);
        REQUIRE (clip.isValid());

        automation = ProjectEdits::findAutomation (h.document.getState(),
                                                   (int) clip[ids::automationId]);
        REQUIRE (automation.isValid());

        auto points = ProjectEdits::sortedAutomationPoints (automation);
        REQUIRE (points.size() == 2);

        points.getFirst().setProperty (ids::value, 0.2, nullptr);
        points.getLast().setProperty (ids::value, 0.8, nullptr);
    }

    juce::ValueTree leftPoint() const
    {
        return ProjectEdits::sortedAutomationPoints (automation).getFirst();
    }

    /** A point on the drawn curve, halfway along the first segment. */
    juce::Point<int> midSegment (int trackIndex = 0) const
    {
        const auto model = curvePointsOf (automation);
        const auto lane = h.playlist.laneGeometryFor (clip, trackIndex);
        const auto step = (model.front().step + model.back().step) * 0.5;

        return lane.positionOf (step, curveValueAt (model, step)).toInt();
    }

    PlaylistHarness& h;
    juce::ValueTree clip, automation;
};

} // namespace

TEST_CASE ("dragging a segment bends it, and does not move the clip", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto startBar = (int) c.clip[ids::startBar];
    const auto at = c.midSegment();

    h.playlist.mouseDown (eventAt (h.playlist, at));
    h.playlist.mouseDrag (eventAt (h.playlist, at.translated (0, -60), 1, {}, true));
    h.playlist.mouseUp (eventAt (h.playlist, at.translated (0, -60), 1, {}, true));

    INFO ("bend " << (double) c.leftPoint()[ids::curve]);
    REQUIRE (std::abs ((double) c.leftPoint()[ids::curve]) > 0.05);

    // The clip itself did not move: a bend is a press ON the curve, and only
    // within a few pixels of it.
    REQUIRE ((int) c.clip[ids::startBar] == startBar);
}

TEST_CASE ("a press away from the curve still moves the clip", "[ui][playlist][automation]")
{
    // The gesture the bend must not take away. There is a test that says an
    // automation clip moves like any other, and this is the same promise stated
    // against the new branch order.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto lane = h.playlist.laneGeometryFor (c.clip, 0);

    // Top-left of the clip, well above a curve that runs from 0.2 to 0.8.
    const auto away = lane.positionOf (1.0, 1.0).toInt();
    REQUIRE (juce::approximatelyEqual ((double) c.leftPoint()[ids::curve], 0.0));

    h.playlist.mouseDown (eventAt (h.playlist, away));
    h.playlist.mouseDrag (eventAt (h.playlist, away.translated (0, -60), 1, {}, true));
    h.playlist.mouseUp (eventAt (h.playlist, away.translated (0, -60), 1, {}, true));

    // It bent nothing.
    REQUIRE (juce::approximatelyEqual ((double) c.leftPoint()[ids::curve], 0.0));
}

TEST_CASE ("shift makes a bend finer", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto bendAfterDrag = [] (bool fine)
    {
        PlaylistHarness h;
        CurveHarness c { h };

        const auto at = c.midSegment();
        const auto mods = fine ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier)
                               : juce::ModifierKeys();

        h.playlist.mouseDown (eventAt (h.playlist, at, 1, mods));
        h.playlist.mouseDrag (eventAt (h.playlist, at.translated (0, -60), 1, mods, true));
        h.playlist.mouseUp (eventAt (h.playlist, at.translated (0, -60), 1, mods, true));

        return std::abs ((double) c.leftPoint()[ids::curve]);
    };

    const auto coarse = bendAfterDrag (false);
    const auto fine = bendAfterDrag (true);

    INFO ("coarse " << coarse << " fine " << fine);
    REQUIRE (coarse > 0.0);
    REQUIRE (fine < coarse);
    REQUIRE_THAT (fine / coarse, WithinAbs (gesture::fineMultiplier, 0.02));
}

TEST_CASE ("a bend out and back returns to where it started", "[ui][playlist][automation]")
{
    // Path-independence, sampled across the WHOLE interaction rather than at its
    // ends. It is what an absolute drag buys over an accumulated one, and the
    // only thing that catches a gesture that drifts.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto at = c.midSegment();
    const auto before = (double) c.leftPoint()[ids::curve];

    h.playlist.mouseDown (eventAt (h.playlist, at));

    juce::Array<double> path;

    for (const auto dy : { -10, -25, -50, -70, -50, -25, -10, 0 })
    {
        h.playlist.mouseDrag (eventAt (h.playlist, at.translated (0, dy), 1, {}, true));
        path.add ((double) c.leftPoint()[ids::curve]);
    }

    h.playlist.mouseUp (eventAt (h.playlist, at, 1, {}, true));

    // It actually moved on the way out, or returning proves nothing.
    REQUIRE (std::abs (path[3] - before) > 0.05);
    REQUIRE_THAT (path.getLast(), WithinAbs (before, 1e-9));
}

TEST_CASE ("a bend drag is one undo step", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto at = c.midSegment();

    h.playlist.mouseDown (eventAt (h.playlist, at));

    for (const auto dy : { -10, -20, -30, -40, -50 })
        h.playlist.mouseDrag (eventAt (h.playlist, at.translated (0, dy), 1, {}, true));

    h.playlist.mouseUp (eventAt (h.playlist, at.translated (0, -50), 1, {}, true));

    REQUIRE (std::abs ((double) c.leftPoint()[ids::curve]) > 0.05);

    // One undo puts the whole gesture back, not one frame of it.
    h.document.getUndoManager().undo();
    REQUIRE_THAT ((double) c.leftPoint()[ids::curve], WithinAbs (0.0, 1e-9));
}

TEST_CASE ("dragging a point past its neighbour stops at it", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto lane = h.playlist.laneGeometryFor (c.clip, 0);
    const auto points = ProjectEdits::sortedAutomationPoints (c.automation);

    const auto from = lane.positionOf ((double) points.getFirst()[ids::step],
                                       (double) points.getFirst()[ids::value])
                          .toInt();

    // Drag the first point far past the last one.
    h.playlist.mouseDown (eventAt (h.playlist, from));
    h.playlist.mouseDrag (
        eventAt (h.playlist, { (int) lane.bounds.getRight() + 200, from.y }, 1, {}, true));
    h.playlist.mouseUp (
        eventAt (h.playlist, { (int) lane.bounds.getRight() + 200, from.y }, 1, {}, true));

    const auto after = ProjectEdits::sortedAutomationPoints (c.automation);
    REQUIRE (after.size() == 2);

    // Still first, and still short of the one it was dragged at.
    REQUIRE (after.getFirst() == points.getFirst());
    REQUIRE ((double) after.getFirst()[ids::step] < (double) after.getLast()[ids::step]);
}

TEST_CASE ("right-clicking a segment offers the three shapes", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto items = h.playlist.clipMenuItemsAt (c.midSegment());

    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Line"));
    REQUIRE (items.contains ("Curve"));
    REQUIRE (items.contains ("Step"));

    // Not "Delete point": there is no point under the pointer, and an item that
    // deleted an adjacent one would do something nobody aimed at.
    REQUIRE_FALSE (items.contains ("Delete point"));
}

TEST_CASE ("choosing Step writes the shape on the segment's left point",
           "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    // 8 is ClipMenuItem::shapeStep, numbered explicitly so a test can name it.
    REQUIRE (h.playlist.applyClipMenuChoiceAt (c.midSegment(), 8));
    REQUIRE (c.leftPoint()[ids::shape].toString() == "step");

    // Re-aimed, because the segment is now DRAWN somewhere else: a step holds
    // the left value rather than running to the right one, and the hit test
    // measures against what is drawn. Aiming where the ramp used to be would be
    // aiming at empty lane.
    ProjectEdits::setPointCurve (c.leftPoint(), 0.8, nullptr);

    REQUIRE (h.playlist.applyClipMenuChoiceAt (c.midSegment(), 6));
    REQUIRE (c.leftPoint()[ids::shape].toString() == "curve");
    REQUIRE_THAT ((double) c.leftPoint()[ids::curve], WithinAbs (0.0, 1e-9));
}

TEST_CASE ("the menu on a point still offers Delete point", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;
    CurveHarness c { h };

    const auto lane = h.playlist.laneGeometryFor (c.clip, 0);
    const auto onPoint = lane.positionOf ((double) c.leftPoint()[ids::step],
                                          (double) c.leftPoint()[ids::value])
                             .toInt();

    const auto items = h.playlist.clipMenuItemsAt (onPoint);

    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Delete point"));
    REQUIRE (items.contains ("Step"));
}

TEST_CASE ("an automation clip can still be moved and deleted like any other",
           "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(),
                                     firstChannelVolumeTarget (h.document.getState()));
    const auto clip = h.playlist.createAutomationClip (target, 0, 2);
    REQUIRE (clip.isValid());

    int trackIndex = -1;

    for (int i = 0; i < h.playlist.getNumTracks(); ++i)
        for (const auto& candidate : h.track (i))
            if (candidate == clip)
                trackIndex = i;

    // Somewhere inside the clip that is not on a point.
    const auto bounds = h.playlist.getBoundsForClip (clip, trackIndex);
    const auto grab = juce::Point<int> ((int) (bounds.getX() + bounds.getWidth() * 0.5f),
                                        (int) bounds.getBottom() - 3);

    h.playlist.mouseDown (eventAt (h.playlist, grab));
    h.playlist.mouseDrag (eventAt (h.playlist, { grab.x + 200, grab.y }));
    h.playlist.mouseUp (eventAt (h.playlist, { grab.x + 200, grab.y }));

    REQUIRE ((int) clip[ids::startBar] > 0);
}

TEST_CASE ("a taller track gives the automation curve the whole lane",
           "[ui][playlist][height][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(),
                                     firstChannelVolumeTarget (h.document.getState()));
    const auto clip = h.playlist.createAutomationClip (target, 0, 4);
    REQUIRE (clip.isValid());

    auto automation = ProjectEdits::findAutomation (h.document.getState(),
                                                    (int) clip[ids::automationId]);
    const auto points = ProjectEdits::sortedAutomationPoints (automation);
    REQUIRE (points.size() == 2);

    points.getFirst().setProperty (ids::value, 0.0, nullptr);
    points.getLast().setProperty (ids::value, 1.0, nullptr);

    const auto spread = [&] (int trackIndex)
    {
        return std::abs (h.playlist.pointPosition (clip, trackIndex, points.getLast()).y
                         - h.playlist.pointPosition (clip, trackIndex, points.getFirst()).y);
    };

    h.playlist.setTrackHeight (tokens::size::trackHeightMin);
    const auto tight = spread (0);

    h.playlist.setTrackHeight (tokens::size::trackHeightMax);
    const auto roomy = spread (0);

    INFO ("value axis: " << tight << "px at the minimum, " << roomy << "px at the maximum");
    REQUIRE (roomy > tight * 4.0f);
}

TEST_CASE ("an automation clip is drawn in its target's function colour",
           "[ui][playlist][automation][role]")
{
    // Every automation clip used to be amber whatever it drove, so a lane of
    // them said only "these are curves" - which their shape already said.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto& project = h.document.getState();
    const auto channel = project.getChildWithName (ids::CHANNEL)[ids::name].toString();

    h.playlist.createAutomationClip (targetNamed (project, channel + " > Volume"), 0, 4);
    h.playlist.refresh();
    h.playlist.resized();

    // Deliberately NOT asserting the absence of `warning`, which is what these
    // used to be drawn in: warning and the playhead are seven degrees apart and
    // coverageOf matches within 24 per channel, so such an assertion passed or
    // failed on whether the playhead was in frame. And the two function colours
    // are close by design, so it is which one WINS that says anything.
    const std::vector<juce::Colour> choices { tokens::colour::funcLevel,
                                              tokens::colour::funcStereo };

    const auto volumeLane = render (h.playlist);

    CHECK (coverageOf (volumeLane, tokens::colour::funcLevel) > 0.0f);
    CHECK (strongestCoverage (volumeLane, choices) == 0);

    // The same clip pointed somewhere else is a different colour, which is the
    // whole claim: the colour comes from the TARGET, not from the clip kind.
    PlaylistHarness pan;
    const auto& panProject = pan.document.getState();
    const auto panChannel = panProject.getChildWithName (ids::CHANNEL)[ids::name].toString();

    pan.playlist.createAutomationClip (targetNamed (panProject, panChannel + " > Pan"), 0, 4);
    pan.playlist.refresh();
    pan.playlist.resized();

    const auto panLane = render (pan.playlist);

    CHECK (coverageOf (panLane, tokens::colour::funcStereo) > 0.0f);
    CHECK (strongestCoverage (panLane, choices) == 1);
}
