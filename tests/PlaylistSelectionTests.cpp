// The span selected on the ruler, and the zoom it is read at.
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
