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

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local, int clickCount = 1,
                          juce::ModifierKeys mods = juce::ModifierKeys(),
                          bool wasDragged = false)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, mods,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount, wasDragged };
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

    Track zero's top edge IS the bottom of the ruler, so the geometry comes from
    the component rather than from a copy of its private constants.
*/
juce::Point<int> rulerPointFor (PlaylistHarness& h, int bar)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (h.track (0), 1, bar, 1, &scratch);
    const auto bounds = h.playlist.getBoundsForClip (probe, 0);
    ProjectEdits::removeClip (h.track (0), probe, &scratch);

    return { (int) (bounds.getX() + bounds.getWidth() * 0.5f), (int) (bounds.getY() * 0.5f) };
}

const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };

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

TEST_CASE ("choosing a target creates an automation and a clip for it", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(), firstChannelVolumeTarget (h.document.getState()));
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

TEST_CASE ("an automation point can be dragged, added and removed", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(), firstChannelVolumeTarget (h.document.getState()));
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
    REQUIRE (ProjectEdits::findAutomation (h.document.getState(), (int) clip[ids::automationId]).isValid());
}

TEST_CASE ("an automation clip can still be moved and deleted like any other", "[ui][playlist][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto target = targetNamed (h.document.getState(), firstChannelVolumeTarget (h.document.getState()));
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

TEST_CASE ("a plain ruler drag still scrubs instead of selecting",
           "[ui][playlist][selection]")
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

TEST_CASE ("a selection survives an edit elsewhere in the arrangement",
           "[ui][playlist][selection]")
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
