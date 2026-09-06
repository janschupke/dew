// One height for every lane, and the three ways of changing it.
//
// Split out of a PlaylistTests.cpp that was 1,788 lines. The fixture every
// one of them uses is PlaylistHarness.h.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

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

        // The indicator keeps its own size rather than stretching to the lane.
        auto* toggle = findDescendantWithID (*header, "trackEnabled");
        INFO ("at height " << height);
        REQUIRE (toggle != nullptr);
        REQUIRE (toggle->getHeight() <= tokens::size::letterToggle);
        REQUIRE (header->getLocalBounds().contains (toggle->getBounds()));

        // And it stays on the TOP rung, beside the name, at every height. It
        // used to drop to a second row past trackHeightRoomy, so the one thing
        // a header says about a lane was in a different place depending on how
        // tall the lane was.
        REQUIRE (toggle->getY() < tokens::size::rowHeight);
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

TEST_CASE ("a trackpad's fragments of a notch add up to a notch", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    // The defect this exists for: a lane height is a whole pixel and a zoom is
    // a FACTOR, so lround(34 * 2^(0.002 * 3)) is 34 and every event a trackpad
    // sends rounded straight back to the height it started at. The gesture was
    // implemented, tested and shipped, and it did nothing at all on the device
    // most of this app is driven with - the mouse-wheel test above passed
    // because one notch clears the rounding in a single event.
    const auto wheel = [] (float deltaY)
    {
        juce::MouseWheelDetails w {};
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = true;
        w.isInertial = false;
        return w;
    };

    const auto at = pointFor (h, 1, 0);
    const auto before = h.playlist.getTrackHeight();

    const auto crossZoom = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    // The magnitude matters and is not a round number picked for tidiness. JUCE
    // reports a precise scrolling delta as pixels over 512, so a slow trackpad
    // drag arrives as events of a couple of thousandths - and at 0.002 the
    // factor is 1.004, which of a 34px lane asks for 34.14 and rounds back to
    // 34. Anything larger clears the rounding in one event and proves nothing.
    for (int i = 0; i < 100; ++i)
        h.playlist.mouseWheelMove (eventAt (h.playlist, at, 1, crossZoom), wheel (0.002f));

    INFO ("height went from " << before << " to " << h.playlist.getTrackHeight());
    CHECK (h.playlist.getTrackHeight() > before);

    // And back down again, which is the other half: a fraction that only ever
    // accumulated upward would make the gesture one-way.
    const auto taller = h.playlist.getTrackHeight();

    for (int i = 0; i < 100; ++i)
        h.playlist.mouseWheelMove (eventAt (h.playlist, at, 1, crossZoom), wheel (-0.002f));

    CHECK (h.playlist.getTrackHeight() < taller);
}

TEST_CASE ("fragments of a wheel notch land where one notch lands", "[ui][playlist][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Path independence, on the axis the resize drag already asserts it on. A
    // hundred events of a two-hundredth is one event of a half, because a zoom
    // composes: (2^(0.002 * k))^100 is 2^(0.2 * k).
    const auto heightAfter = [] (int events, float deltaY)
    {
        PlaylistHarness h;

        juce::MouseWheelDetails w {};
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = true;
        w.isInertial = false;

        const auto at = pointFor (h, 1, 0);
        const auto crossZoom = juce::ModifierKeys::commandModifier
                               | juce::ModifierKeys::shiftModifier;

        for (int i = 0; i < events; ++i)
            h.playlist.mouseWheelMove (eventAt (h.playlist, at, 1, crossZoom), w);

        return h.playlist.getTrackHeight();
    };

    // Within a pixel: the two paths agree in the real number and can round to
    // either side of the same half.
    const auto whole = heightAfter (1, 0.2f);
    const auto fragments = heightAfter (100, 0.002f);

    INFO ("one notch: " << whole << "  twenty fragments: " << fragments);
    CHECK (std::abs (whole - fragments) <= 1);
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

TEST_CASE ("the pointer says what a clip will do", "[ui][playlist][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    juce::UndoManager undo;
    ProjectEdits::addClip (h.track (0), 1, 1, 2, &undo);
    h.playlist.refresh();
    h.playlist.resized();

    const auto cursorAt = [&h] (juce::Point<int> at)
    {
        h.playlist.mouseMove (eventAt (h.playlist, at));
        return h.playlist.getMouseCursor();
    };

    const auto clip = ProjectEdits::findClipAtBar (h.track (0), 1);
    REQUIRE (clip.isValid());

    const auto bounds = h.playlist.getBoundsForClip (clip, 0);

    // A clip's BODY is what a person drags, and it was the one gesture with no
    // cursor at all: only the resize edge said anything.
    CHECK (
        cursorAt ({ (int) (bounds.getX() + bounds.getWidth() * 0.3f), (int) bounds.getCentreY() })
        == cursor::move);

    CHECK (cursorAt ({ (int) bounds.getRight() - 2, (int) bounds.getCentreY() })
           == cursor::resizeX);

    // Control case: an empty lane is not draggable, and says so. Without this
    // the two checks above would pass on a component that returned `move` for
    // every point in the arrangement.
    CHECK (cursorAt (pointFor (h, 6, 1)) == cursor::idle);

    CHECK (cursorAt (rulerPointFor (h, 1)) == cursor::clickable);

    // A tool outranks what is under the pointer: with the paint tool a clip is
    // somewhere to put one, not something to pick up.
    h.playlist.getToolbar().setTool (PlaylistTool::paint);
    CHECK (
        cursorAt ({ (int) (bounds.getX() + bounds.getWidth() * 0.3f), (int) bounds.getCentreY() })
        == cursor::nib);
    h.playlist.getToolbar().setTool (PlaylistTool::select);

    // And it is given back when the pointer leaves, or a window edge keeps a
    // resize arrow that means nothing there.
    h.playlist.mouseMove (
        eventAt (h.playlist, { (int) bounds.getRight() - 2, (int) bounds.getCentreY() }));
    REQUIRE (h.playlist.getMouseCursor() == cursor::resizeX);

    h.playlist.mouseExit (eventAt (h.playlist, { -1, -1 }));
    CHECK (h.playlist.getMouseCursor() == cursor::idle);
}

TEST_CASE ("a pinch reads the same modifiers a wheel notch does", "[ui][playlist][height]")
{
    /*  mouseMagnify ignored event.mods outright, in all three views. So the one
        gesture on a trackpad that IS a zoom could only ever reach the time
        axis, while the wheel - which needs a modifier to zoom at all - could
        reach both. Command-shift is "the other axis" everywhere else in dew.

        Both halves, because either alone passes for the wrong reason: a pinch
        that had simply been re-pointed at the lanes would fail the control
        case below it.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto at = pointFor (h, 1, 0);
    const auto crossZoom = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    const auto lanesBefore = h.playlist.getTrackHeight();
    const auto timeBefore = h.playlist.getTimeline().pixelsPerStep;

    h.playlist.mouseMagnify (eventAt (h.playlist, at, 1, crossZoom), 2.0f);

    INFO ("lane " << lanesBefore << " -> " << h.playlist.getTrackHeight());
    CHECK (h.playlist.getTrackHeight() > lanesBefore);

    // And it left the time axis alone, which is the half that says this is a
    // second axis rather than a re-pointed gesture.
    CHECK (juce::exactlyEqual (h.playlist.getTimeline().pixelsPerStep, timeBefore));

    // The control case: a bare pinch still zooms time and leaves the lanes.
    const auto tallerLanes = h.playlist.getTrackHeight();

    h.playlist.mouseMagnify (eventAt (h.playlist, at), 2.0f);

    CHECK (h.playlist.getTimeline().pixelsPerStep > timeBefore);
    CHECK (h.playlist.getTrackHeight() == tallerLanes);
}
