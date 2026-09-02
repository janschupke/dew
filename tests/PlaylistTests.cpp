#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "engine/EngineSnapshot.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/PlaylistComponent.h"

using namespace dew;

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

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local, int clickCount = 1)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, juce::ModifierKeys(),
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount, false };
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
    REQUIRE ((int) moved[ids::lengthBars] == 2);   // it kept its length
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
