#include "ProjectEdits.h"

#include "Ids.h"
#include "ProjectSchema.h"

namespace dew
{

namespace
{

juce::ValueTree findChildWithId (const juce::ValueTree& parent, const juce::Identifier& type, int id)
{
    for (const auto& child : parent)
        if (child.hasType (type) && (int) child[ids::id] == id)
            return child;

    return {};
}

} // namespace

juce::ValueTree ProjectEdits::findChannel (const juce::ValueTree& project, int channelId)
{
    return findChildWithId (project, ids::CHANNEL, channelId);
}

juce::ValueTree ProjectEdits::findPattern (const juce::ValueTree& project, int patternId)
{
    return findChildWithId (project, ids::PATTERN, patternId);
}

juce::ValueTree ProjectEdits::findMixerTrack (const juce::ValueTree& project, int mixerTrackId)
{
    return findChildWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK, mixerTrackId);
}

juce::ValueTree ProjectEdits::findNote (const juce::ValueTree& pattern, int channelId, int step, int pitch)
{
    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE)
            && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step
            && (int) note[ids::pitch] == pitch)
            return note;

    return {};
}

juce::ValueTree ProjectEdits::findNoteAtStep (const juce::ValueTree& pattern, int channelId, int step)
{
    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE)
            && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step)
            return note;

    return {};
}

int ProjectEdits::nextFreeId (const juce::ValueTree& project, const juce::Identifier& childType)
{
    int highest = 0;

    const auto scan = [&] (const juce::ValueTree& parent)
    {
        for (const auto& child : parent)
            if (child.hasType (childType))
                highest = juce::jmax (highest, (int) child[ids::id]);
    };

    scan (project);
    scan (project.getChildWithName (ids::MIXER));

    return highest + 1;
}

bool ProjectEdits::toggleStep (juce::ValueTree pattern, int channelId, int step, int pitch,
                               juce::UndoManager* undo)
{
    // A lit step means "any note on this channel here", so clicking a step that
    // a piano-roll note happens to start on clears that note. Anything else
    // would leave the grid showing a cell the user cannot switch off.
    if (auto existing = findNoteAtStep (pattern, channelId, step); existing.isValid())
    {
        removeNote (pattern, existing, undo);
        return false;
    }

    addNote (pattern, channelId, step, 1, pitch, 1.0f, undo);
    return true;
}

juce::ValueTree ProjectEdits::addNote (juce::ValueTree pattern, int channelId, int step,
                                       int lengthSteps, int pitch, float velocity,
                                       juce::UndoManager* undo)
{
    juce::ValueTree note (ids::NOTE);
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, juce::jmax (0, step), nullptr);
    note.setProperty (ids::lengthSteps, juce::jmax (1, lengthSteps), nullptr);
    note.setProperty (ids::pitch, juce::jlimit (0, 127, pitch), nullptr);
    note.setProperty (ids::velocity, (double) juce::jlimit (0.0f, 1.0f, velocity), nullptr);

    pattern.appendChild (note, undo);
    return note;
}

void ProjectEdits::removeNote (juce::ValueTree pattern, juce::ValueTree note, juce::UndoManager* undo)
{
    const auto index = pattern.indexOf (note);

    if (index >= 0)
        pattern.removeChild (index, undo);
}

void ProjectEdits::moveNote (juce::ValueTree note, int newStep, int newPitch, juce::UndoManager* undo)
{
    note.setProperty (ids::step, juce::jmax (0, newStep), undo);
    note.setProperty (ids::pitch, juce::jlimit (0, 127, newPitch), undo);
}

void ProjectEdits::resizeNote (juce::ValueTree note, int newLengthSteps, juce::UndoManager* undo)
{
    note.setProperty (ids::lengthSteps, juce::jmax (1, newLengthSteps), undo);
}

void ProjectEdits::setNoteVelocity (juce::ValueTree note, double velocity, juce::UndoManager* undo)
{
    // Zero velocity is a note that exists but cannot be heard, which reads as a
    // bug rather than an edit; the floor keeps a quiet note audible.
    note.setProperty (ids::velocity, juce::jlimit (0.05, 1.0, velocity), undo);
}

bool ProjectEdits::growPatternToFitNotes (juce::ValueTree pattern, juce::UndoManager* undo)
{
    if (! pattern.isValid())
        return false;

    const auto needed = lengthNeededForNotes (pattern);

    if (needed <= (int) pattern[ids::lengthSteps])
        return false;

    pattern.setProperty (ids::lengthSteps, needed, undo);
    return true;
}

juce::ValueTree ProjectEdits::addChannel (juce::ValueTree project, const juce::String& name,
                                          juce::UndoManager* undo)
{
    auto channel = defaultTreeFor (childSpecFor (projectSpec(), "channels"));

    const auto id = nextFreeId (project, ids::CHANNEL);
    channel.setProperty (ids::id, id, nullptr);
    channel.setProperty (ids::name, name.isNotEmpty() ? name : "Channel " + juce::String (id), nullptr);

    // Route to a mixer track if one with a matching number exists, else insert 1.
    const auto mixer = project.getChildWithName (ids::MIXER);
    auto routed = findChildWithId (mixer, ids::MIXER_TRACK, id);

    if (! routed.isValid())
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK))
            {
                routed = track;
                break;
            }

    channel.setProperty (ids::mixerTrackId, routed.isValid() ? (int) routed[ids::id] : 1, nullptr);

    // Channels are a schema array: insert after the last existing one so the
    // canonical child order is preserved.
    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::CHANNEL))
            insertAt = i + 1;

    project.addChild (channel, insertAt, undo);
    return channel;
}

void ProjectEdits::removeChannel (juce::ValueTree project, juce::ValueTree channel,
                                  juce::UndoManager* undo)
{
    if (! channel.isValid())
        return;

    const auto channelId = (int) channel[ids::id];

    // Notes referring to a channel that no longer exists would be dropped by
    // the next snapshot with a warning. Remove them with the channel instead,
    // so the document stays consistent and the change is one undo step.
    for (auto pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        for (int i = pattern.getNumChildren(); --i >= 0;)
        {
            const auto note = pattern.getChild (i);

            if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
                pattern.removeChild (i, undo);
        }
    }

    const auto index = project.indexOf (channel);

    if (index >= 0)
        project.removeChild (index, undo);
}

juce::ValueTree ProjectEdits::addPattern (juce::ValueTree project, juce::UndoManager* undo)
{
    auto pattern = defaultTreeFor (childSpecFor (projectSpec(), "patterns"));

    const auto id = nextFreeId (project, ids::PATTERN);
    pattern.setProperty (ids::id, id, nullptr);
    pattern.setProperty (ids::name, "Pattern " + juce::String (id), nullptr);

    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::PATTERN))
            insertAt = i + 1;

    project.addChild (pattern, insertAt, undo);
    return pattern;
}

juce::ValueTree ProjectEdits::duplicatePattern (juce::ValueTree project, juce::ValueTree pattern,
                                                juce::UndoManager* undo)
{
    if (! pattern.isValid() || ! pattern.hasType (ids::PATTERN))
        return {};

    const auto index = project.indexOf (pattern);

    if (index < 0)
        return {};

    // createCopy is a deep copy, so the notes come along; only the identity has
    // to change.
    auto copy = pattern.createCopy();

    const auto sourceId = (int) pattern[ids::id];
    const auto newId    = nextFreeId (project, ids::PATTERN);

    copy.setProperty (ids::id, newId, nullptr);

    // An auto-named pattern gets the next auto name; a renamed one keeps the
    // name it was given, marked as a copy, because that name is information.
    const auto sourceName = pattern[ids::name].toString();
    const auto autoName   = "Pattern " + juce::String (sourceId);

    copy.setProperty (ids::name,
                      sourceName == autoName ? "Pattern " + juce::String (newId)
                                             : sourceName + " copy",
                      nullptr);

    // Next to the original, so the pattern list reads in the order it was built.
    project.addChild (copy, index + 1, undo);
    return copy;
}

bool ProjectEdits::removePattern (juce::ValueTree project, juce::ValueTree pattern,
                                  juce::UndoManager* undo)
{
    if (! pattern.isValid() || ! pattern.hasType (ids::PATTERN))
        return false;

    const auto index = project.indexOf (pattern);

    if (index < 0)
        return false;

    int patternCount = 0;

    for (const auto& child : project)
        if (child.hasType (ids::PATTERN))
            ++patternCount;

    // A project with no patterns has nothing to edit and nothing to play.
    if (patternCount <= 1)
        return false;

    const auto patternId = (int) pattern[ids::id];

    // Clips referring to a pattern that no longer exists would be dropped by the
    // next snapshot with a warning. Remove them here so the document stays
    // consistent and the whole deletion is one undo step.
    const auto playlist = project.getChildWithName (ids::PLAYLIST);

    for (auto track : playlist)
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (int i = track.getNumChildren(); --i >= 0;)
        {
            const auto clip = track.getChild (i);

            if (clip.hasType (ids::CLIP) && (int) clip[ids::patternId] == patternId)
                track.removeChild (i, undo);
        }
    }

    project.removeChild (index, undo);
    return true;
}

int ProjectEdits::lengthNeededForNotes (const juce::ValueTree& pattern)
{
    int needed = 1;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE))
            needed = juce::jmax (needed, (int) note[ids::step] + (int) note[ids::lengthSteps]);

    return needed;
}

int ProjectEdits::countEffects (const juce::ValueTree& owner)
{
    int count = 0;

    for (const auto& child : owner)
        if (child.hasType (ids::EFFECT))
            ++count;

    return count;
}

juce::Array<juce::ValueTree> ProjectEdits::effectChainOwners (const juce::ValueTree& project)
{
    juce::Array<juce::ValueTree> owners;

    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
            owners.add (child);

    for (const auto& track : project.getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK))
            owners.add (track);

    return owners;
}

juce::ValueTree ProjectEdits::addEffect (juce::ValueTree project, juce::ValueTree owner,
                                         const juce::String& type, juce::UndoManager* undo)
{
    if (! owner.isValid() || countEffects (owner) >= kMaxEffectsPerChain)
        return {};

    auto effect = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "channels"), "effects"));
    effect.setProperty (ids::type, type, nullptr);

    // Effect ids are unique across the whole project, not per chain: the engine
    // keys each effect's DSP state on its id, so two effects sharing one would
    // fight over the same reverb tank.
    int highest = 0;

    for (const auto& chainOwner : effectChainOwners (project))
        for (const auto& existing : chainOwner)
            if (existing.hasType (ids::EFFECT))
                highest = juce::jmax (highest, (int) existing[ids::id]);

    effect.setProperty (ids::id, highest + 1, nullptr);

    owner.appendChild (effect, undo);
    return effect;
}

void ProjectEdits::removeEffect (juce::ValueTree owner, juce::ValueTree effect, juce::UndoManager* undo)
{
    const auto index = owner.indexOf (effect);

    if (index >= 0)
        owner.removeChild (index, undo);
}

void ProjectEdits::moveEffect (juce::ValueTree owner, juce::ValueTree effect, int newPosition,
                               juce::UndoManager* undo)
{
    const auto count = countEffects (owner);

    if (count <= 1)
        return;

    const auto target = juce::jlimit (0, count - 1, newPosition);

    // Positions are counted among effects, but ValueTree indices count every
    // child - a channel also holds its instrument - so translate.
    int seen = 0;
    int targetIndex = -1;

    for (int i = 0; i < owner.getNumChildren(); ++i)
        if (owner.getChild (i).hasType (ids::EFFECT) && seen++ == target)
            targetIndex = i;

    const auto from = owner.indexOf (effect);

    if (from >= 0 && targetIndex >= 0 && from != targetIndex)
        owner.moveChild (from, targetIndex, undo);
}

juce::ValueTree ProjectEdits::addClip (juce::ValueTree playlistTrack, int patternId, int startBar,
                                       int lengthBars, juce::UndoManager* undo)
{
    juce::ValueTree clip (ids::CLIP);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, juce::jmax (0, startBar), nullptr);
    clip.setProperty (ids::lengthBars, juce::jmax (1, lengthBars), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

void ProjectEdits::removeClip (juce::ValueTree playlistTrack, juce::ValueTree clip,
                               juce::UndoManager* undo)
{
    const auto index = playlistTrack.indexOf (clip);

    if (index >= 0)
        playlistTrack.removeChild (index, undo);
}

juce::ValueTree ProjectEdits::moveClipToTrack (juce::ValueTree fromTrack, juce::ValueTree clip,
                                               juce::ValueTree toTrack, int newStartBar,
                                               juce::UndoManager* undo)
{
    if (! clip.isValid() || ! toTrack.isValid())
        return clip;

    if (fromTrack == toTrack)
    {
        moveClip (clip, newStartBar, undo);
        return clip;
    }

    // Copy first: removing the child drops the only reference the caller may
    // hold, and a detached tree carries its properties but no parent to undo to.
    auto moved = clip.createCopy();
    moved.setProperty (ids::startBar, juce::jmax (0, newStartBar), nullptr);

    const auto index = fromTrack.indexOf (clip);

    if (index >= 0)
        fromTrack.removeChild (index, undo);

    toTrack.appendChild (moved, undo);
    return moved;
}

void ProjectEdits::moveClip (juce::ValueTree clip, int newStartBar, juce::UndoManager* undo)
{
    clip.setProperty (ids::startBar, juce::jmax (0, newStartBar), undo);
}

void ProjectEdits::resizeClip (juce::ValueTree clip, int newLengthBars, juce::UndoManager* undo)
{
    clip.setProperty (ids::lengthBars, juce::jmax (1, newLengthBars), undo);
}

int ProjectEdits::barsNeededForClips (const juce::ValueTree& project)
{
    int needed = 1;

    for (const auto& track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                needed = juce::jmax (needed, (int) clip[ids::startBar]
                                                 + juce::jmax (1, (int) clip[ids::lengthBars]));
    }

    return needed;
}

bool ProjectEdits::growSongToFitClips (juce::ValueTree project, juce::UndoManager* undo)
{
    const auto needed = barsNeededForClips (project);

    if (needed <= (int) project[ids::barsInSong])
        return false;

    project.setProperty (ids::barsInSong, needed, undo);
    return true;
}

juce::ValueTree ProjectEdits::findClipAtBar (const juce::ValueTree& playlistTrack, int bar)
{
    for (const auto& clip : playlistTrack)
    {
        if (! clip.hasType (ids::CLIP))
            continue;

        const auto start = (int) clip[ids::startBar];
        const auto end = start + juce::jmax (1, (int) clip[ids::lengthBars]);

        if (bar >= start && bar < end)
            return clip;
    }

    return {};
}

} // namespace dew
