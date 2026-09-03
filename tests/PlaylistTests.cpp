#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "engine/EngineSnapshot.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/design/Tokens.h"
#include "model/AutomationCurve.h"
#include "ui/AutomationLane.h"
#include "ui/design/Gestures.h"
#include "ui/PlaylistComponent.h"

#include "PaintProbe.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

namespace
{

struct PlaylistHarness
{
    PlaylistHarness (int width = 1200, int height = 500)
    {
        document.setState (ProjectFactory::createDefault(), true);
        playlist.setSize (width, height);
        playlist.setVisible (true);
        playlist.refresh();
        playlist.resized();
    }

    juce::ValueTree track (int index)
    {
        int i = 0;

        for (const auto& t : document.getState().getChildWithName (ids::PLAYLIST))
            if (t.hasType (ids::PLAYLIST_TRACK) && i++ == index)
                return t;

        return {};
    }

    int countClips (int trackIndex)
    {
        int n = 0;

        for (const auto& clip : track (trackIndex))
            if (clip.hasType (ids::CLIP))
                ++n;

        return n;
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    PlaylistComponent playlist { document, engine, editorState };
};

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local, int clickCount = 1,
                          juce::ModifierKeys mods = juce::ModifierKeys(), bool wasDragged = false)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             mods,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             0.0f,
             &target,
             &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount,
             wasDragged };
}

/** The centre of a bar on a track, asked of the component rather than
    recomputed, so the tests cannot drift from the layout.
*/
juce::Point<int> pointFor (PlaylistHarness& h, int bar, int trackIndex)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (h.track (trackIndex), 1, bar, 1, &scratch);
    const auto bounds = h.playlist.getBoundsForClip (probe, trackIndex);
    ProjectEdits::removeClip (h.track (trackIndex), probe, &scratch);

    return { (int) (bounds.getX() + bounds.getWidth() * 0.35f), (int) bounds.getCentreY() };
}

/** A point on the ruler above a bar.

    Both coordinates come from the component: the x from where it would paint a
    clip in that bar, the y from the ruler strip it publishes. This used to take
    half of track zero's top edge, which was the middle of the ruler only for as
    long as the ruler started at the very top of the component - it does not,
    now there is a tool strip above it.
*/
juce::Point<int> rulerPointFor (PlaylistHarness& h, int bar)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (h.track (0), 1, bar, 1, &scratch);
    const auto bounds = h.playlist.getBoundsForClip (probe, 0);
    ProjectEdits::removeClip (h.track (0), probe, &scratch);

    return { (int) (bounds.getX() + bounds.getWidth() * 0.5f),
             h.playlist.getRulerArea().getCentreY() };
}

const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };

/** Component::findChildWithID is NOT recursive, and the track headers and the
    add-track button live one level down inside the playlist's header holder -
    the component that exists to CLIP them once a lane can be scrolled.
*/
juce::Component* findDescendantWithID (juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* found = findDescendantWithID (*child, id))
            return found;
    }

    return nullptr;
}

} // namespace

TEST_CASE ("clicking an empty bar places the current pattern there", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    REQUIRE (h.countClips (0) == 0);

    const auto at = pointFor (h, 1, 0);
    h.playlist.mouseDown (eventAt (h.playlist, at));
    h.playlist.mouseUp (eventAt (h.playlist, at));

    REQUIRE (h.countClips (0) == 1);
    REQUIRE ((int) h.track (0).getChild (0)[ids::startBar] == 1);
}

TEST_CASE ("a clip can be dragged onto another track", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addClip (h.track (0), 1, 0, 2, &undo);

    REQUIRE (h.countClips (0) == 1);
    REQUIRE (h.countClips (2) == 0);

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 0, 0)));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 2, 2)));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 2, 2)));

    REQUIRE (h.countClips (0) == 0);
    REQUIRE (h.countClips (2) == 1);

    const auto moved = h.track (2).getChild (0);
    REQUIRE ((int) moved[ids::startBar] == 2);
    REQUIRE ((int) moved[ids::lengthBars] == 2); // it kept its length
}

TEST_CASE ("a drag across several tracks keeps following the clip", "[ui][playlist]")
{
    // Crossing a track replaces the clip's tree. If the drag kept editing the
    // old one, the second crossing would silently do nothing.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addClip (h.track (0), 1, 0, 1, &undo);

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 0, 0)));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 0, 1)));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 0, 2)));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 1, 3)));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 1, 3)));

    REQUIRE (h.countClips (0) == 0);
    REQUIRE (h.countClips (1) == 0);
    REQUIRE (h.countClips (2) == 0);
    REQUIRE (h.countClips (3) == 1);
    REQUIRE ((int) h.track (3).getChild (0)[ids::startBar] == 1);
}

TEST_CASE ("double-clicking a clip opens its pattern", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    const auto second = ProjectEdits::addPattern (h.document.getState(), &undo);
    const auto secondId = (int) second[ids::id];

    ProjectEdits::addClip (h.track (1), secondId, 2, 1, &undo);

    bool asked = false;
    h.playlist.onOpenPatternInPianoRoll = [&asked] { asked = true; };

    h.editorState.setCurrentPatternId (1);
    REQUIRE (h.editorState.getCurrentPatternId() == 1);

    h.playlist.mouseDoubleClick (eventAt (h.playlist, pointFor (h, 2, 1), 2));

    REQUIRE (h.editorState.getCurrentPatternId() == secondId);
    REQUIRE (asked);
}

TEST_CASE ("double-clicking empty space opens nothing", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    bool asked = false;
    h.playlist.onOpenPatternInPianoRoll = [&asked] { asked = true; };

    h.playlist.mouseDoubleClick (eventAt (h.playlist, pointFor (h, 2, 1), 2));

    REQUIRE (! asked);
}

TEST_CASE ("a clip dropped past the end of the song lengthens the song", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto barsBefore = (int) h.document.getState()[ids::barsInSong];

    // Somewhere well past the end, but still on screen: the bars beyond the song
    // are painted as inert and remain writable.
    const auto at = pointFor (h, barsBefore + 2, 0);
    h.playlist.mouseDown (eventAt (h.playlist, at));
    h.playlist.mouseUp (eventAt (h.playlist, at));

    REQUIRE (h.countClips (0) == 1);
    REQUIRE ((int) h.track (0).getChild (0)[ids::startBar] == barsBefore + 2);
    REQUIRE ((int) h.document.getState()[ids::barsInSong] == barsBefore + 3);

    // Growing is one-way, as it is for pattern length.
    juce::UndoManager& undo = h.document.getUndoManager();
    h.document.getState().setProperty (ids::barsInSong, 64, &undo);
    REQUIRE (! ProjectEdits::growSongToFitClips (h.document.getState(), &undo));
    REQUIRE ((int) h.document.getState()[ids::barsInSong] == 64);
}

TEST_CASE ("a muted playlist track silences its clips in the engine", "[ui][playlist][engine]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addClip (h.track (0), 1, 0, 1, &undo);
    ProjectEdits::addClip (h.track (1), 1, 0, 1, &undo);

    const auto audibleClips = [&h]
    {
        const auto snapshot = buildSnapshot (h.document.getState(), nullptr);
        int audible = 0;

        for (const auto& clip : snapshot.clips)
            if (clip.trackAudible)
                ++audible;

        return audible;
    };

    REQUIRE (audibleClips() == 2);

    h.track (0).setProperty (ids::mute, true, &undo);
    REQUIRE (audibleClips() == 1);

    // Solo on a different track silences the rest, and mute still beats solo.
    h.track (0).setProperty (ids::mute, false, &undo);
    h.track (1).setProperty (ids::solo, true, &undo);
    REQUIRE (audibleClips() == 1);

    h.track (1).setProperty (ids::mute, true, &undo);
    REQUIRE (audibleClips() == 0);
}

// --- automation clips --------------------------------------------------------

#include "model/AutomationTargets.h"

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

// --- the selected span --------------------------------------------------------

TEST_CASE ("shift-dragging the ruler selects a span of bars", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    REQUIRE_FALSE (h.editorState.hasBarSelection());

    h.playlist.mouseDown (eventAt (h.playlist, rulerPointFor (h, 2), 1, shift));
    h.playlist.mouseDrag (eventAt (h.playlist, rulerPointFor (h, 5), 1, shift, true));
    h.playlist.mouseUp (eventAt (h.playlist, rulerPointFor (h, 5), 1, shift, true));

    REQUIRE (h.editorState.hasBarSelection());

    // Half-open, so bars 2 to 5 inclusive is [2, 6).
    REQUIRE (h.editorState.getSelectedBarRange().getStart() == 2);
    REQUIRE (h.editorState.getSelectedBarRange().getEnd() == 6);
}

TEST_CASE ("a span can be dragged out backwards", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.playlist.mouseDown (eventAt (h.playlist, rulerPointFor (h, 6), 1, shift));
    h.playlist.mouseDrag (eventAt (h.playlist, rulerPointFor (h, 3), 1, shift, true));
    h.playlist.mouseUp (eventAt (h.playlist, rulerPointFor (h, 3), 1, shift, true));

    // The anchor is where the drag began, not the lower of the two bars.
    REQUIRE (h.editorState.getSelectedBarRange().getStart() == 3);
    REQUIRE (h.editorState.getSelectedBarRange().getEnd() == 7);
}

TEST_CASE ("shift-clicking the ruler without dragging clears the selection",
           "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.editorState.setSelectedBarRange ({ 1, 4 });
    REQUIRE (h.editorState.hasBarSelection());

    const auto at = rulerPointFor (h, 2);
    h.playlist.mouseDown (eventAt (h.playlist, at, 1, shift));
    h.playlist.mouseUp (eventAt (h.playlist, at, 1, shift));

    REQUIRE_FALSE (h.editorState.hasBarSelection());
}

TEST_CASE ("a plain ruler drag still scrubs instead of selecting", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.playlist.mouseDown (eventAt (h.playlist, rulerPointFor (h, 3)));
    h.playlist.mouseDrag (eventAt (h.playlist, rulerPointFor (h, 5), 1, {}, true));
    h.playlist.mouseUp (eventAt (h.playlist, rulerPointFor (h, 5), 1, {}, true));

    // Scrubbing was on the ruler first; selecting had to fit around it.
    REQUIRE_FALSE (h.editorState.hasBarSelection());
}

TEST_CASE ("selecting a span never touches the document or the undo stack",
           "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    // Work the geometry out FIRST: rulerPointFor places and removes a probe clip
    // to ask the component where a bar is, which dirties the document by itself.
    const auto from = rulerPointFor (h, 1);
    const auto to = rulerPointFor (h, 4);

    h.document.setChangedFlag (false);
    REQUIRE_FALSE (h.document.hasChangedSinceSaved());

    h.playlist.mouseDown (eventAt (h.playlist, from, 1, shift));
    h.playlist.mouseDrag (eventAt (h.playlist, to, 1, shift, true));
    h.playlist.mouseUp (eventAt (h.playlist, to, 1, shift, true));

    REQUIRE (h.editorState.hasBarSelection());

    // A selection is a view of the project, not part of it.
    REQUIRE_FALSE (h.document.hasChangedSinceSaved());
    REQUIRE_FALSE (h.document.getUndoManager().canUndo());
}

TEST_CASE ("a selection survives an edit elsewhere in the arrangement", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.editorState.setSelectedBarRange ({ 2, 5 });

    auto& undo = h.document.getUndoManager();
    ProjectEdits::addClip (h.track (1), 1, 7, 1, &undo);
    h.playlist.refresh();

    REQUIRE (h.editorState.getSelectedBarRange() == juce::Range<int> (2, 5));
}

TEST_CASE ("the selected span is painted", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto render = [&h]
    {
        juce::Image image (juce::Image::ARGB, h.playlist.getWidth(), h.playlist.getHeight(), true);
        juce::Graphics g (image);
        h.playlist.paintEntireComponent (g, true);
        return image;
    };

    const auto before = render();

    h.editorState.setSelectedBarRange ({ 1, 6 });

    const auto after = render();

    // Count pixels that actually changed. A "does it have any blue in it" test
    // cannot tell an accent wash from the background, which already does.
    int changed = 0;

    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = 0; x < before.getWidth(); ++x)
            if (before.getPixelAt (x, y) != after.getPixelAt (x, y))
                ++changed;

    INFO ("changed pixels: " << changed);
    REQUIRE (changed > 1000);
}

TEST_CASE ("double-clicking the playlist ruler clears the selection", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.editorState.setSelectedBarRange ({ 1, 4 });
    REQUIRE (h.editorState.hasBarSelection());

    h.playlist.mouseDoubleClick (eventAt (h.playlist, rulerPointFor (h, 2), 2));

    REQUIRE_FALSE (h.editorState.hasBarSelection());
}

TEST_CASE ("mod-clicking the playlist ruler spans from the playhead", "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.playlist.mouseDown (eventAt (h.playlist, rulerPointFor (h, 3), 1, mod));
    h.playlist.mouseUp (eventAt (h.playlist, rulerPointFor (h, 3), 1, mod));

    REQUIRE (h.editorState.hasBarSelection());

    // From where the transport is - bar zero here - to the bar clicked, and the
    // range is half-open, so clicking bar 3 includes it.
    const auto selection = h.editorState.getSelectedBarRange();
    INFO ("selection " << selection.getStart() << " -> " << selection.getEnd());
    REQUIRE (selection.getStart() == 0);
    REQUIRE (selection.getEnd() == 4);
}

TEST_CASE ("a mod-click on the playlist ruler does not move the transport",
           "[ui][playlist][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.playlist.mouseDown (eventAt (h.playlist, rulerPointFor (h, 3), 1, mod));
    h.playlist.mouseUp (eventAt (h.playlist, rulerPointFor (h, 3), 1, mod));

    // It selects instead of scrubbing, or the span would always start where the
    // click landed and "from the playhead" would mean nothing.
    REQUIRE (juce::exactlyEqual (h.engine.getPlayheadSteps(), 0.0));
}

// --- zoom, tools and copies ---------------------------------------------------

TEST_CASE ("the playlist zooms, and a resize does not undo it", "[ui][playlist][zoom]")
{
    // Zoom used to be a formula - the song's length divided into the window -
    // recomputed on every resized(). So it could not be zoomed at all: any
    // change, a clip moved or a track added, put it straight back.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto fitted = h.playlist.getTimeline().pixelsPerStep;
    REQUIRE (fitted > 0.0);

    auto& toolbar = h.playlist.getToolbar();
    REQUIRE (toolbar.onZoom != nullptr);

    toolbar.onZoom (1.5);
    const auto zoomed = h.playlist.getTimeline().pixelsPerStep;
    REQUIRE (zoomed > fitted);

    h.playlist.resized();
    CHECK (juce::exactlyEqual (h.playlist.getTimeline().pixelsPerStep, zoomed));

    // Adding a track is a change that used to re-fit as well.
    h.playlist.addTrack();
    CHECK (juce::exactlyEqual (h.playlist.getTimeline().pixelsPerStep, zoomed));

    toolbar.onZoom (0.0);
    CHECK (h.playlist.getTimeline().pixelsPerStep < zoomed);
    CHECK (juce::exactlyEqual (h.playlist.getTimeline().scrollOffsetSteps, 0.0));
}

TEST_CASE ("zoomed out, the view reaches past the end of the song", "[ui][playlist][zoom]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    // Zoom right in, so the song is much wider than the window and there is
    // somewhere to scroll to.
    for (int i = 0; i < 6; ++i)
        h.playlist.getToolbar().onZoom (1.5);

    auto& timeline = const_cast<TimelineView&> (h.playlist.getTimeline());
    timeline.scrollOffsetSteps = 1e6;
    h.playlist.resized();

    const auto bars = juce::jmax (4, (int) h.document.getState()[ids::barsInSong]);
    const auto offset = h.playlist.getTimeline().scrollOffsetSteps;

    // Clamped to a screen short of the end, so the last bar can be worked on
    // with empty space beside it rather than jammed against the window edge -
    // which is what the piano roll has always done and the playlist could not,
    // because the re-fit put the scroll back to zero every time.
    INFO ("scrolled to " << offset << " of " << bars << " bars");
    CHECK (offset > 0.0);
    CHECK (offset < (double) bars);
}

TEST_CASE ("the paint tool lays a clip in every bar it crosses", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    REQUIRE (h.countClips (0) == 0);

    h.playlist.setTool (PlaylistTool::paint);

    const auto from = pointFor (h, 1, 0);
    const auto to = pointFor (h, 4, 0);

    h.playlist.mouseDown (eventAt (h.playlist, from));

    for (int bar = 2; bar <= 4; ++bar)
        h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, bar, 0), 1, {}, true));

    h.playlist.mouseUp (eventAt (h.playlist, to, 1, {}, true));

    INFO ("clips on track 0: " << h.countClips (0));
    CHECK (h.countClips (0) == 4);

    for (int bar = 1; bar <= 4; ++bar)
        CHECK (ProjectEdits::findClipAtBar (h.track (0), bar).isValid());
}

TEST_CASE ("the paint tool does not stack a clip on one already there", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager setup;
    ProjectEdits::addClip (h.track (0), 1, 2, 1, &setup);

    h.playlist.setTool (PlaylistTool::paint);

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 1, 0)));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 2, 0), 1, {}, true));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 3, 0), 1, {}, true));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 3, 0), 1, {}, true));

    // Three bars crossed, one of them already occupied.
    CHECK (h.countClips (0) == 3);
}

TEST_CASE ("a mod-drag copies a clip and leaves the original", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager setup;
    auto original = ProjectEdits::addClip (h.track (0), 1, 1, 1, &setup);
    const auto patternId = (int) original[ids::patternId];

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 1, 0), 1, mod));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 5, 0), 1, mod, true));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 5, 0), 1, mod, true));

    INFO ("clips on track 0: " << h.countClips (0));
    CHECK (h.countClips (0) == 2);

    // The original stays where it was, and both name the same pattern - a plain
    // copy is another instance of the same phrase.
    auto stayed = ProjectEdits::findClipAtBar (h.track (0), 1);
    auto copy = ProjectEdits::findClipAtBar (h.track (0), 5);

    REQUIRE (stayed.isValid());
    REQUIRE (copy.isValid());
    CHECK ((int) copy[ids::patternId] == patternId);
}

TEST_CASE ("a mod-drag that never moves makes no copy", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager setup;
    ProjectEdits::addClip (h.track (0), 1, 1, 1, &setup);

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 1, 0), 1, mod));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 1, 0), 1, mod));

    // Otherwise a mod-press litters a copy directly on top of its own original.
    CHECK (h.countClips (0) == 1);
}

TEST_CASE ("a mod-shift-drag gives the copy a pattern of its own", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager setup;
    auto original = ProjectEdits::addClip (h.track (0), 1, 1, 1, &setup);
    const auto patternId = (int) original[ids::patternId];

    // Something in the pattern, so the copy can be shown to have carried it.
    auto pattern = ProjectEdits::findPattern (h.document.getState(), patternId);
    ProjectEdits::addNote (pattern, 1, 3, 1, 64, 0.8f, &setup);

    const juce::ModifierKeys modShift { juce::ModifierKeys::commandModifier
                                        | juce::ModifierKeys::shiftModifier };

    h.playlist.mouseDown (eventAt (h.playlist, pointFor (h, 1, 0), 1, modShift));
    h.playlist.mouseDrag (eventAt (h.playlist, pointFor (h, 5, 0), 1, modShift, true));
    h.playlist.mouseUp (eventAt (h.playlist, pointFor (h, 5, 0), 1, modShift, true));

    auto copy = ProjectEdits::findClipAtBar (h.track (0), 5);
    REQUIRE (copy.isValid());

    const auto freshId = (int) copy[ids::patternId];
    INFO ("original pattern " << patternId << ", copy's " << freshId);

    // Its own pattern, so editing this repeat does not edit every other one.
    CHECK (freshId != patternId);

    auto fresh = ProjectEdits::findPattern (h.document.getState(), freshId);
    REQUIRE (fresh.isValid());

    int notes = 0;

    for (const auto& note : fresh)
        if (note.hasType (ids::NOTE))
            ++notes;

    CHECK (notes == 1);
}

TEST_CASE ("the clip menu duplicates a pattern, for that clip alone", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager setup;
    ProjectEdits::addClip (h.track (0), 1, 2, 1, &setup);
    auto other = ProjectEdits::addClip (h.track (1), 1, 2, 1, &setup);

    const auto items = h.playlist.clipMenuItems (0, 2);
    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Duplicate pattern"));

    REQUIRE (h.playlist.applyClipMenuChoice (0, 2, 5));

    auto changed = ProjectEdits::findClipAtBar (h.track (0), 2);
    REQUIRE (changed.isValid());

    // Only the clip it was asked about. A pattern is shared by every clip that
    // names it, so duplicating for one must not re-point the rest.
    CHECK ((int) changed[ids::patternId] != 1);
    CHECK ((int) other[ids::patternId] == 1);
}

// --- track rows and the clip menu --------------------------------------------

TEST_CASE ("the add-track button is the row after the last track", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    auto* button = findDescendantWithID (h.playlist, "addTrackButton");
    REQUIRE (button != nullptr);
    REQUIRE (button->isVisible());

    // In the header column, below every track row.
    REQUIRE (button->getX() >= 0);
    REQUIRE (button->getRight() <= tokens::size::gutterTrack);

    // Asked of the component, at whatever height a lane currently is. The
    // button's own y is relative to the header holder, so the holder's top is
    // added back - the holder is a clipping parent, not a scroller, and carries
    // no offset of its own.
    auto* holder = button->getParentComponent();
    REQUIRE (holder != nullptr);

    const auto height = h.playlist.getTrackHeight();
    const auto lastTrackBottom = h.playlist.getNumTracks() * height;

    REQUIRE (holder->getY() == h.playlist.getRulerArea().getBottom());
    REQUIRE (button->getY() >= lastTrackBottom);
    REQUIRE (button->getY() < lastTrackBottom + height);
}

TEST_CASE ("one control sets the height of every track", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager scratch;
    auto a = ProjectEdits::addClip (h.track (0), 1, 0, 1, &scratch);
    auto d = ProjectEdits::addClip (h.track (3), 1, 0, 1, &scratch);

    h.playlist.setTrackHeight (90);
    REQUIRE (h.playlist.getTrackHeight() == 90);

    // Uniform: the first track and the fourth are the same height.
    REQUIRE_THAT ((double) h.playlist.getBoundsForClip (a, 0).getHeight(), WithinAbs (90.0, 1e-6));
    REQUIRE_THAT ((double) h.playlist.getBoundsForClip (d, 3).getHeight(), WithinAbs (90.0, 1e-6));

    // And contiguous: lane 1 begins exactly where lane 0 ends. A gap or an
    // overlap here is a lane you can click into and reach the wrong track.
    auto b = ProjectEdits::addClip (h.track (1), 1, 0, 1, &scratch);
    REQUIRE_THAT ((double) h.playlist.getBoundsForClip (b, 1).getY(),
                  WithinAbs ((double) h.playlist.getBoundsForClip (a, 0).getBottom(), 1e-6));
}

TEST_CASE ("the track height is clamped to what a lane can show", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.playlist.setTrackHeight (1);
    REQUIRE (h.playlist.getTrackHeight() == tokens::size::trackHeightMin);

    h.playlist.setTrackHeight (10000);
    REQUIRE (h.playlist.getTrackHeight() == tokens::size::trackHeightMax);
}

TEST_CASE ("hit testing follows the track height", "[ui][playlist][height]")
{
    // The test that catches a rowHeight left behind in trackAtY: at 90px a
    // click aimed at track 2 lands on track 2 and nowhere else.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.playlist.setTrackHeight (90);

    const auto at = pointFor (h, 2, 2);
    h.playlist.mouseDown (eventAt (h.playlist, at));
    h.playlist.mouseUp (eventAt (h.playlist, at));

    REQUIRE (h.countClips (2) == 1);
    REQUIRE (h.countClips (0) == 0);
    REQUIRE (h.countClips (1) == 0);
    REQUIRE (h.countClips (3) == 0);
}

TEST_CASE ("the toolbar's height buttons change every track at once", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto before = h.playlist.getTrackHeight();

    // The seam the user reaches, the same way the zoom test drives zoom.
    h.playlist.getToolbar().onTrackHeight (1.5);
    REQUIRE (h.playlist.getTrackHeight() > before);

    h.playlist.getToolbar().onTrackHeight (1.0 / 1.5);
    REQUIRE (h.playlist.getTrackHeight() == before);

    // Zero means fit, exactly as it does for zoom.
    h.playlist.getToolbar().onTrackHeight (0.0);

    const auto rows = h.playlist.getNumTracks() + 1;
    REQUIRE ((rows * h.playlist.getTrackHeight() <= h.playlist.getLaneArea().getHeight()
              || h.playlist.getTrackHeight() == tokens::size::trackHeightMin));
}

TEST_CASE ("the last track is reachable when the tracks overflow", "[ui][playlist][height]")
{
    // The test that proves the height control is a feature and not a trap.
    // Without vertical scrolling, making the lanes taller simply puts the last
    // tracks somewhere the pointer cannot go.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    while (h.playlist.getNumTracks() < 12)
        h.playlist.addTrack();

    h.playlist.setTrackHeight (tokens::size::trackHeightMax);

    const auto lanes = h.playlist.getLaneArea();
    REQUIRE (12 * h.playlist.getTrackHeight() > lanes.getHeight());

    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (h.track (11), 1, 0, 1, &scratch);

    // Out of the view to begin with...
    REQUIRE_FALSE (h.playlist.getBoundsForClip (probe, 11).toNearestInt().intersects (lanes));

    ProjectEdits::removeClip (h.track (11), probe, &scratch);

    // ...and inside it once scrolled to the bottom.
    h.playlist.scrollTracksTo (1e9);

    const auto at = pointFor (h, 1, 11);
    REQUIRE (lanes.contains (at));

    h.playlist.mouseDown (eventAt (h.playlist, at));
    h.playlist.mouseUp (eventAt (h.playlist, at));

    REQUIRE (h.countClips (11) == 1);
}

TEST_CASE ("the add-track button is still reachable when the tracks overflow",
           "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    while (h.playlist.getNumTracks() < 12)
        h.playlist.addTrack();

    h.playlist.setTrackHeight (tokens::size::trackHeightMax);
    h.playlist.scrollTracksTo (1e9);

    auto* button = findDescendantWithID (h.playlist, "addTrackButton");
    REQUIRE (button != nullptr);
    REQUIRE (button->isVisible());

    // The holder is what CLIPS, so "reachable" means "inside the holder's own
    // bounds" - not inside getLaneArea(), which is the strip to the right of the
    // gutter and never contains a header at all.
    auto* holder = button->getParentComponent();
    REQUIRE (holder != nullptr);
    REQUIRE (holder->getLocalBounds().contains (button->getBounds()));

    // And the holder itself is where the lanes are.
    REQUIRE (holder->getY() == h.playlist.getRulerArea().getBottom());
    REQUIRE (holder->getBottom() <= h.playlist.getHeight());
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

TEST_CASE ("the header lays out at every height", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    for (const auto height : { tokens::size::trackHeightMin, tokens::size::trackHeightRoomy,
                               tokens::size::trackHeightMax })
    {
        h.playlist.setTrackHeight (height);

        auto* header = findDescendantWithID (h.playlist, "playlistTrackHeader");
        REQUIRE (header != nullptr);
        REQUIRE (header->getHeight() == height);

        // The toggles keep their own size rather than stretching to the lane.
        for (const auto* id : { "trackMute", "trackSolo" })
        {
            auto* toggle = findDescendantWithID (*header, id);
            INFO ("at height " << height << ", looking for " << id);
            REQUIRE (toggle != nullptr);
            REQUIRE (toggle->getHeight() <= tokens::size::letterToggle);
            REQUIRE (header->getLocalBounds().contains (toggle->getBounds()));
        }
    }
}

TEST_CASE ("a project load does not reset the track height", "[ui][playlist][height]")
{
    // The payoff of keeping the height on the view rather than in the document,
    // asserted rather than asserted about.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    h.playlist.setTrackHeight (90);
    h.document.setState (ProjectFactory::createDefault(), true);
    h.playlist.refresh();

    REQUIRE (h.playlist.getTrackHeight() == 90);
}

TEST_CASE ("a track row's context menu offers rename, add and remove", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto items = h.playlist.trackMenuItems (0);

    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Rename"));
    REQUIRE (items.contains ("Colour"));
    REQUIRE (items.contains ("Add track"));
    REQUIRE (items.contains ("Reset track height"));
    REQUIRE (items.contains ("Remove track"));

    // Remove is fenced off by a separator, because it is the destructive one.
    // The ITEM BEFORE it rather than the first separator in the menu: there are
    // two now, and asking for the first was asking where the menu happened to
    // be divided rather than what it is that has to be divided off.
    REQUIRE (items[items.indexOf ("Remove track") - 1] == "-");

    REQUIRE_FALSE (h.playlist.applyTrackMenuChoice (99, 1));
}

TEST_CASE ("a track can be added and removed from the playlist", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto before = h.playlist.getNumTracks();
    REQUIRE (before == 4);

    h.playlist.addTrack();
    REQUIRE (h.playlist.getNumTracks() == before + 1);

    // The row acts on ITSELF - "Remove track" on the third row removes the third
    // track, whatever else is going on.
    REQUIRE (h.playlist.applyTrackMenuChoice (2, 3));
    REQUIRE (h.playlist.getNumTracks() == before);

    REQUIRE (h.document.getUndoManager().undo());
    REQUIRE (h.playlist.getNumTracks() == before + 1);
}

TEST_CASE ("right-clicking a clip offers a menu rather than deleting it", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    auto track = h.track (0);
    ProjectEdits::addClip (track, 1, 0, 2, nullptr);
    h.playlist.refresh();

    REQUIRE (h.countClips (0) == 1);

    const auto clip = ProjectEdits::findClipAtBar (track, 0);
    const auto bounds = h.playlist.getBoundsForClip (clip, 0);
    const auto at = bounds.getCentre().toInt();

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.playlist.mouseDown (eventAt (h.playlist, at, 1, rightButton));
    h.playlist.mouseUp (eventAt (h.playlist, at, 1, rightButton));

    // The clip is still there: deleting is now something you choose from the
    // menu rather than something that happens on the way past.
    REQUIRE (h.countClips (0) == 1);

    const auto items = h.playlist.clipMenuItems (0, 0);
    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Open pattern"));
    REQUIRE (items.contains ("Delete clip"));
}

TEST_CASE ("alt-clicking a clip still deletes it outright", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    auto track = h.track (0);
    ProjectEdits::addClip (track, 1, 0, 2, nullptr);
    h.playlist.refresh();

    REQUIRE (h.countClips (0) == 1);

    const auto clip = ProjectEdits::findClipAtBar (track, 0);
    const auto at = h.playlist.getBoundsForClip (clip, 0).getCentre().toInt();
    const juce::ModifierKeys alt { juce::ModifierKeys::altModifier };

    h.playlist.mouseDown (eventAt (h.playlist, at, 1, alt));
    h.playlist.mouseUp (eventAt (h.playlist, at, 1, alt));

    // The sweep-to-clear gesture the piano roll and step grid share survives.
    REQUIRE (h.countClips (0) == 0);
}

TEST_CASE ("the clip menu deletes the clip it was opened on", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    auto track = h.track (0);
    ProjectEdits::addClip (track, 1, 0, 2, nullptr);
    ProjectEdits::addClip (track, 1, 4, 2, nullptr);
    h.playlist.refresh();

    REQUIRE (h.countClips (0) == 2);

    REQUIRE (h.playlist.applyClipMenuChoice (0, 4, 2));

    REQUIRE (h.countClips (0) == 1);
    REQUIRE (ProjectEdits::findClipAtBar (h.track (0), 0).isValid());
    REQUIRE_FALSE (ProjectEdits::findClipAtBar (h.track (0), 4).isValid());
}

TEST_CASE ("right-clicking empty lane space offers to add a clip there", "[ui][playlist]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    REQUIRE (h.countClips (0) == 0);

    const auto items = h.playlist.clipMenuItems (0, 2);
    INFO ("items: " << items.joinIntoString (", "));
    REQUIRE (items.contains ("Add clip here"));
    REQUIRE_FALSE (items.contains ("Delete clip"));

    REQUIRE (h.playlist.applyClipMenuChoice (0, 2, 4));
    REQUIRE (h.countClips (0) == 1);
    REQUIRE (ProjectEdits::findClipAtBar (h.track (0), 2).isValid());
}

namespace
{

/** Every track header, in lane order. findChildWithID is not recursive and the
    headers live two levels down, inside the clipping holder.
*/
juce::Array<juce::Component*> trackHeaders (juce::Component& root)
{
    juce::Array<juce::Component*> found;

    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == "playlistTrackHeader")
            found.add (child);

        found.addArray (trackHeaders (*child));
    }

    return found;
}

/** A point on a header's bottom edge, asked of the header rather than
    recomputed from the lane height. */
juce::Point<int> onResizeEdge (juce::Component& header)
{
    return { header.getWidth() / 2, header.getHeight() - 1 };
}

} // namespace

TEST_CASE ("the add-track button is a button at every lane height", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    auto* addButton = findDescendantWithID (h.playlist, "addTrackButton");
    REQUIRE (addButton != nullptr);

    const auto atDefault = addButton->getHeight();

    // It used to take the whole row, so at the tallest lane height "+ Track" was
    // a two-hundred-pixel rectangle with a word in the middle of it.
    h.playlist.setTrackHeight (tokens::size::trackHeightMax);
    REQUIRE (h.playlist.getTrackHeight() == tokens::size::trackHeightMax);

    CHECK (addButton->getHeight() == atDefault);
    CHECK (addButton->getHeight() <= tokens::size::rowHeight);

    // And it is still the row after the last track, which is where the track it
    // adds will appear - that is the part that must not change.
    const auto headers = trackHeaders (h.playlist);
    REQUIRE (! headers.isEmpty());

    CHECK (addButton->getY() == headers.getLast()->getBottom() + tokens::space::xs);
}

TEST_CASE ("dragging a header's bottom edge resizes every lane", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto headers = trackHeaders (h.playlist);
    REQUIRE (headers.size() >= 2);

    const auto before = h.playlist.getTrackHeight();
    auto& first = *headers[0];

    // The pointer shape is the whole affordance: nothing on screen says the
    // edge is grabbable until the cursor is over it.
    first.mouseMove (eventAt (first, onResizeEdge (first)));
    CHECK (first.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);

    first.mouseMove (eventAt (first, { first.getWidth() / 2, 0 }));
    CHECK (first.getMouseCursor() == juce::MouseCursor::NormalCursor);

    const auto grab = onResizeEdge (first);
    first.mouseDown (eventAt (first, grab));
    first.mouseDrag (eventAt (first, grab.translated (0, 40), 1, {}, true));
    first.mouseUp (eventAt (first, grab.translated (0, 40), 1, {}, true));

    // The FIRST lane's edge moved by 40, and one lane sits above it, so every
    // lane is 40 taller.
    CHECK (h.playlist.getTrackHeight() == before + 40);
}

TEST_CASE ("a resize drag is path-independent", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The same bug the effect chain's reorder has: a drag that accumulates
    // between samples drifts, so where you END UP depends on how you got there.
    // Sampled two ways to the same point, it must land on the same height.
    const auto heightAfter = [] (const juce::Array<int>& path)
    {
        PlaylistHarness h;
        const auto headers = trackHeaders (h.playlist);
        REQUIRE (headers.size() >= 1);

        auto& first = *headers[0];
        const auto grab = onResizeEdge (first);

        first.mouseDown (eventAt (first, grab));

        for (const auto dy : path)
            first.mouseDrag (eventAt (first, grab.translated (0, dy), 1, {}, true));

        first.mouseUp (eventAt (first, grab.translated (0, path.getLast()), 1, {}, true));

        return h.playlist.getTrackHeight();
    };

    CHECK (heightAfter ({ 60 }) == heightAfter ({ 10, 20, 40, 60 }));
    CHECK (heightAfter ({ 60 }) == heightAfter ({ 90, 5, 120, 60 }));
}

TEST_CASE ("a grab away from the edge is still a press on the track", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto headers = trackHeaders (h.playlist);
    REQUIRE (headers.size() >= 1);

    auto& first = *headers[0];
    const auto before = h.playlist.getTrackHeight();

    // Consuming every press would have taken the row's own gestures away.
    first.mouseDown (eventAt (first, { first.getWidth() / 2, 0 }));
    first.mouseDrag (eventAt (first, { first.getWidth() / 2, 40 }, 1, {}, true));
    first.mouseUp (eventAt (first, { first.getWidth() / 2, 40 }, 1, {}, true));

    CHECK (h.playlist.getTrackHeight() == before);
}

TEST_CASE ("each wheel modifier moves a different axis", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto wheel = [] (float deltaY)
    {
        juce::MouseWheelDetails w {};
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    };

    const auto at = pointFor (h, 1, 0);
    const auto heightBefore = h.playlist.getTrackHeight();
    const auto zoomBefore = h.playlist.getTimeline().pixelsPerStep;

    // Zoom plus shift is the OTHER axis, and it has to be checked before zoom -
    // which it also satisfies.
    h.playlist.mouseWheelMove (
        eventAt (h.playlist, at, 1,
                 juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
        wheel (0.5f));

    CHECK (h.playlist.getTrackHeight() > heightBefore);
    CHECK (juce::exactlyEqual (h.playlist.getTimeline().pixelsPerStep, zoomBefore));

    // And plain zoom still zooms time and leaves the lanes alone.
    const auto heightNow = h.playlist.getTrackHeight();

    h.playlist.mouseWheelMove (eventAt (h.playlist, at, 1, juce::ModifierKeys::commandModifier),
                               wheel (0.5f));

    CHECK (h.playlist.getTimeline().pixelsPerStep > zoomBefore);
    CHECK (h.playlist.getTrackHeight() == heightNow);
}
