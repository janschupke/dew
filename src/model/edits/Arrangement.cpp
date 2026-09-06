// =============================================================================
// The mixer's inserts, the playlist's tracks, and the clips on them.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// Together because removing an insert moves the channels routed into it,
// and a clip is the one object that belongs to a playlist track while
// naming a pattern, an automation or an audio channel.
// =============================================================================

#include "model/ProjectEdits.h"

#include <limits>

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

const NodeSpec& clipSpecFor()
{
    return childSpecFor (childSpecFor (childSpecFor (projectSpec(), "playlist"), "tracks"),
                         "clips");
}

} // namespace

// --- the mixer ---------------------------------------------------------------

int ProjectEdits::countMixerTracks (const juce::ValueTree& project)
{
    int n = 0;

    for (const auto& child : project.getChildWithName (ids::MIXER))
        if (child.hasType (ids::MIXER_TRACK))
            ++n;

    return n;
}

juce::ValueTree ProjectEdits::addMixerTrack (juce::ValueTree project, const juce::String& name,
                                             juce::UndoManager* undo)
{
    auto mixer = project.getChildWithName (ids::MIXER);

    if (! mixer.isValid() || countMixerTracks (project) >= kMaxMixerTracks)
        return {};

    // From the spec rather than by hand, for the reason addPlaylistTrack gives.
    auto track = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "mixer"), "tracks"));

    const auto id = nextFreeId (project, ids::MIXER_TRACK);
    track.setProperty (ids::id, id, nullptr);
    track.setProperty (
        ids::name,
        name.isNotEmpty() ? name : tr (StringId::project_insertN, Args {}.with ("number", id)),
        nullptr);

    // Appended, with no insert position to work out: the mixer's schema order is
    // master then tracks*, so the end is already the canonical place.
    mixer.appendChild (track, undo);
    return track;
}

bool ProjectEdits::removeMixerTrack (juce::ValueTree project, juce::ValueTree track,
                                     juce::UndoManager* undo)
{
    // A type check, not a name check, which is what makes the master structurally
    // unremovable rather than conditionally so.
    if (! track.isValid() || ! track.hasType (ids::MIXER_TRACK))
        return false;

    auto mixer = project.getChildWithName (ids::MIXER);
    const auto index = mixer.indexOf (track);

    if (index < 0 || countMixerTracks (project) <= 1)
        return false;

    const auto removedId = (int) track[ids::id];

    // The insert every orphaned channel lands on: the first one that is not this.
    auto survivorId = 0;

    for (const auto& child : mixer)
        if (child.hasType (ids::MIXER_TRACK) && (int) child[ids::id] != removedId)
        {
            survivorId = (int) child[ids::id];
            break;
        }

    if (survivorId == 0)
        return false;

    // In the same transaction as the removal, so a channel is never routed at an
    // insert that is not there - not even for one undo step.
    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL) && (int) channel[ids::mixerTrackId] == removedId)
            channel.setProperty (ids::mixerTrackId, survivorId, undo);

    mixer.removeChild (index, undo);
    return true;
}

juce::ValueTree ProjectEdits::addPlaylistTrack (juce::ValueTree project, const juce::String& name,
                                                juce::UndoManager* undo)
{
    auto playlist = project.getChildWithName (ids::PLAYLIST);

    if (! playlist.isValid())
        return {};

    // From the spec rather than by hand, for the reason addClip gives: a
    // hand-built node stops round-tripping the moment the schema gains a
    // property, and the canonical-shape test is what catches it.
    auto track = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "playlist"), "tracks"));

    int existing = 0;

    for (const auto& child : playlist)
        if (child.hasType (ids::PLAYLIST_TRACK))
            ++existing;

    track.setProperty (ids::name,
                       name.isNotEmpty()
                           ? name
                           : tr (StringId::project_trackN, Args {}.with ("number", existing + 1)),
                       nullptr);

    playlist.appendChild (track, undo);
    return track;
}

void ProjectEdits::removePlaylistTrack (juce::ValueTree project, juce::ValueTree track,
                                        juce::UndoManager* undo)
{
    if (! track.isValid())
        return;

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    const auto index = playlist.indexOf (track);

    if (index >= 0)
        playlist.removeChild (index, undo);
}

juce::ValueTree ProjectEdits::addClip (juce::ValueTree playlistTrack, int patternId, int startStep,
                                       int lengthSteps, juce::UndoManager* undo)
{
    // Built from the spec rather than by hand, so a property added to the
    // schema later is present here too. A hand-built node round-tripped into
    // something different from what the editor made, which the canonical-shape
    // test caught the moment clips gained a `kind`.
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startStep, juce::jmax (0, startStep), nullptr);
    clip.setProperty (ids::lengthSteps, juce::jmax (1, lengthSteps), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

juce::ValueTree ProjectEdits::addAutomationClip (juce::ValueTree playlistTrack, int automationId,
                                                 int startStep, int lengthSteps,
                                                 juce::UndoManager* undo)
{
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "automation", nullptr);
    clip.setProperty (ids::automationId, automationId, nullptr);
    clip.setProperty (ids::startStep, juce::jmax (0, startStep), nullptr);
    clip.setProperty (ids::lengthSteps, juce::jmax (1, lengthSteps), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

juce::ValueTree ProjectEdits::addAudioClip (juce::ValueTree playlistTrack, int channelId,
                                            int startStep, int lengthSteps, juce::UndoManager* undo)
{
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "audio", nullptr);
    clip.setProperty (ids::channelId, channelId, nullptr);
    clip.setProperty (ids::startStep, juce::jmax (0, startStep), nullptr);
    clip.setProperty (ids::lengthSteps, juce::jmax (1, lengthSteps), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

bool ProjectEdits::isAutomationClip (const juce::ValueTree& clip)
{
    return clip[ids::kind].toString() == "automation";
}

bool ProjectEdits::isAudioClip (const juce::ValueTree& clip)
{
    return clip[ids::kind].toString() == "audio";
}

bool ProjectEdits::isMidiClip (const juce::ValueTree& clip)
{
    // By exclusion rather than == "pattern": a version 3 clip has no `kind` at
    // all, and those are notes. The stored value stays "pattern" - the word the
    // schema has always used, and the node a clip of this kind points at.
    return ! isAutomationClip (clip) && ! isAudioClip (clip);
}

void ProjectEdits::removeClip (juce::ValueTree playlistTrack, juce::ValueTree clip,
                               juce::UndoManager* undo)
{
    const auto index = playlistTrack.indexOf (clip);

    if (index >= 0)
        playlistTrack.removeChild (index, undo);
}

juce::ValueTree ProjectEdits::copyClip (juce::ValueTree targetTrack, const juce::ValueTree& clip,
                                        int startStep, juce::UndoManager* undo)
{
    if (! clip.isValid() || ! targetTrack.isValid())
        return {};

    // Whole-node rather than dispatching on the kind: a clip carries what it
    // refers to - a pattern, a channel or an automation lane - and copying the
    // node carries all three without this having to learn the table. Clips have
    // no id of their own, so nothing has to be reassigned.
    auto copy = clip.createCopy();
    copy.setProperty (ids::startStep, juce::jmax (0, startStep), nullptr);

    targetTrack.appendChild (copy, undo);
    return copy;
}

juce::ValueTree ProjectEdits::moveClipToTrack (juce::ValueTree fromTrack, juce::ValueTree clip,
                                               juce::ValueTree toTrack, int newStartStep,
                                               juce::UndoManager* undo)
{
    if (! clip.isValid() || ! toTrack.isValid())
        return clip;

    if (fromTrack == toTrack)
    {
        moveClip (clip, newStartStep, undo);
        return clip;
    }

    // Copy first: removing the child drops the only reference the caller may
    // hold, and a detached tree carries its properties but no parent to undo to.
    auto moved = clip.createCopy();
    moved.setProperty (ids::startStep, juce::jmax (0, newStartStep), nullptr);

    const auto index = fromTrack.indexOf (clip);

    if (index >= 0)
        fromTrack.removeChild (index, undo);

    toTrack.appendChild (moved, undo);
    return moved;
}

void ProjectEdits::moveClip (juce::ValueTree clip, int newStartStep, juce::UndoManager* undo)
{
    clip.setProperty (ids::startStep, juce::jmax (0, newStartStep), undo);
}

void ProjectEdits::resizeClip (juce::ValueTree clip, int newLengthSteps, juce::UndoManager* undo)
{
    clip.setProperty (ids::lengthSteps, juce::jmax (1, newLengthSteps), undo);
}

int ProjectEdits::stepsNeededForClips (const juce::ValueTree& project)
{
    int needed = 0;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                needed = juce::jmax (needed, (int) clip[ids::startStep]
                                                 + juce::jmax (1, (int) clip[ids::lengthSteps]));
    }

    return needed;
}

bool ProjectEdits::growSongToFitClips (juce::ValueTree project, juce::UndoManager* undo)
{
    // The SONG is still counted in bars - it is a container, and the ruler over
    // it numbers bars - so the clips' reach in steps is rounded UP to one.
    // Ceiling rather than rounding: a clip ending one step into a bar needs
    // that bar, and cropping the arrangement to hide it would be worse than an
    // empty bar at the end.
    const auto perBar = juce::jmax (1, Meter::of (project).stepsPerBar());
    const auto needed = juce::jmax (1, (stepsNeededForClips (project) + perBar - 1) / perBar);

    if (needed <= (int) project[ids::barsInSong])
        return false;

    project.setProperty (ids::barsInSong, needed, undo);
    return true;
}

juce::ValueTree ProjectEdits::findClipAtStep (const juce::ValueTree& playlistTrack, int step)
{
    for (const auto& clip : playlistTrack)
    {
        if (! clip.hasType (ids::CLIP))
            continue;

        const auto start = (int) clip[ids::startStep];
        const auto end = start + juce::jmax (1, (int) clip[ids::lengthSteps]);

        if (step >= start && step < end)
            return clip;
    }

    return {};
}

} // namespace dew
