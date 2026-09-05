// Placing, moving, copying and deleting a clip.
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

    // A lane answers for itself. This used to set solo on the OTHER lane and
    // assert that the first went quiet, which is the composed rule that made a
    // lane's audibility a fact about its neighbours; silencing the other lane
    // is how the same thing is asked for now.
    h.track (0).setProperty (ids::mute, false, &undo);
    h.track (1).setProperty (ids::mute, true, &undo);
    REQUIRE (audibleClips() == 1);

    h.track (0).setProperty (ids::mute, true, &undo);
    REQUIRE (audibleClips() == 0);
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

    // About which ROW the menu acts on, not about the question it now asks
    // first, so it answers yes at once.
    h.playlist.confirmDestructive = testing::alwaysConfirm();

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
