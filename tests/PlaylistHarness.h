#pragma once

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/PlaylistComponent.h"

/** Shared by every playlist test file, so four of them cannot drift into aiming
    at the arrangement in four slightly different ways.

    The twin is RollHarness.h. Both put the geometry helpers here rather than in
    the tests, because every one of them asks the COMPONENT where something is
    instead of recomputing the layout beside it - which is the only reason a
    test still passes after the layout changes.
*/
namespace dew::testing
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

inline juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                                 int clickCount = 1, juce::ModifierKeys mods = juce::ModifierKeys(),
                                 bool wasDragged = false)
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
inline juce::Point<int> pointFor (PlaylistHarness& h, int bar, int trackIndex)
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
inline juce::Point<int> rulerPointFor (PlaylistHarness& h, int bar)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addClip (h.track (0), 1, bar, 1, &scratch);
    const auto bounds = h.playlist.getBoundsForClip (probe, 0);
    ProjectEdits::removeClip (h.track (0), probe, &scratch);

    return { (int) (bounds.getX() + bounds.getWidth() * 0.5f),
             h.playlist.getRulerArea().getCentreY() };
}

inline const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };

/** Component::findChildWithID is NOT recursive, and the track headers and the
    add-track button live one level down inside the playlist's header holder -
    the component that exists to CLIP them once a lane can be scrolled.
*/
inline juce::Component* findDescendantWithID (juce::Component& root, const juce::String& id)
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

} // namespace dew::testing
