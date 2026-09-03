#include "model/ProjectEdits.h"

#include "model/ModuleState.h"

#include <cmath>
#include <limits>

#include "model/AutomationCurve.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

juce::ValueTree findChildWithId (const juce::ValueTree& parent, const juce::Identifier& type,
                                 int id)
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

juce::ValueTree ProjectEdits::findNote (const juce::ValueTree& pattern, int channelId, int step,
                                        int pitch)
{
    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step && (int) note[ids::pitch] == pitch)
            return note;

    return {};
}

juce::ValueTree ProjectEdits::findNoteAtStep (const juce::ValueTree& pattern, int channelId,
                                              int step)
{
    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step)
            return note;

    return {};
}

int ProjectEdits::channelIndexForId (const juce::ValueTree& project, int channelId)
{
    int index = 0;

    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if ((int) channel[ids::id] == channelId)
            return index;

        ++index;
    }

    return -1;
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
    auto note = defaultTreeFor (childSpecFor (childSpecFor (projectSpec(), "patterns"), "notes"));
    note.setProperty (ids::ch, channelId, nullptr);
    note.setProperty (ids::step, juce::jmax (0, step), nullptr);
    note.setProperty (ids::lengthSteps, juce::jmax (1, lengthSteps), nullptr);
    note.setProperty (ids::pitch, juce::jlimit (0, 127, pitch), nullptr);
    note.setProperty (ids::velocity, (double) juce::jlimit (0.0f, 1.0f, velocity), nullptr);

    pattern.appendChild (note, undo);
    return note;
}

void ProjectEdits::removeNote (juce::ValueTree pattern, juce::ValueTree note,
                               juce::UndoManager* undo)
{
    const auto index = pattern.indexOf (note);

    if (index >= 0)
        pattern.removeChild (index, undo);
}

void ProjectEdits::moveNote (juce::ValueTree note, int newStep, int newPitch,
                             juce::UndoManager* undo)
{
    note.setProperty (ids::step, juce::jmax (0, newStep), undo);
    note.setProperty (ids::pitch, juce::jlimit (0, 127, newPitch), undo);
}

void ProjectEdits::resizeNote (juce::ValueTree note, int newLengthSteps, juce::UndoManager* undo)
{
    note.setProperty (ids::lengthSteps, juce::jmax (1, newLengthSteps), undo);
}

void ProjectEdits::setProperty (juce::ValueTree node, const juce::Identifier& property,
                                const juce::var& value, juce::UndoManager* undo,
                                const juce::String& transactionName, bool continuingTransaction)
{
    if (! node.isValid())
        return;

    // Nothing to record. A ValueTree write of the value already there still
    // opens a transaction and still pushes an undo step, which is how a
    // refresh() that re-states every control ends up in the undo history.
    if (node[property] == value)
        return;

    if (undo != nullptr && ! continuingTransaction)
        undo->beginNewTransaction (transactionName);

    node.setProperty (property, value, undo);
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
    channel.setProperty (ids::name, name.isNotEmpty() ? name : "Channel " + juce::String (id),
                         nullptr);

    // Round the ramp rather than taking the schema default, which is one blue:
    // every channel a user added came out the same colour as the last, in an
    // application whose channel rack, step grid, piano roll, playlist and mixer
    // all identify a channel BY its colour.
    channel.setProperty (ids::colour, entityColour::defaultHex (id - 1), nullptr);

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

juce::ValueTree ProjectEdits::addAudioChannel (juce::ValueTree project, const juce::String& name,
                                               juce::UndoManager* undo)
{
    auto channel = addChannel (project, name.isNotEmpty() ? name : "Audio", undo);

    // After the insert, so the property change joins the same undo transaction
    // rather than becoming a second step that leaves a synth channel behind.
    channel.setProperty (ids::source, "audio", undo);
    return channel;
}

bool ProjectEdits::isAudioChannel (const juce::ValueTree& channel)
{
    return channel[ids::source].toString() == "audio";
}

void ProjectEdits::setSampleSource (juce::ValueTree channel, const juce::String& path,
                                    int sourceSampleRate, int lengthSamples,
                                    juce::UndoManager* undo)
{
    auto sample = channel.getChildWithName (ids::SAMPLE);

    if (! sample.isValid())
        return;

    sample.setProperty (ids::file, path, undo);
    sample.setProperty (ids::sourceSampleRate, juce::jmax (1, sourceSampleRate), undo);
    sample.setProperty (ids::lengthSamples, juce::jmax (0, lengthSamples), undo);

    // A new source invalidates the old trim, and leaving it would silence a
    // recording whose predecessor was trimmed to a shorter region.
    sample.setProperty (ids::startSample, 0, undo);
    sample.setProperty (ids::endSample, 0, undo);
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
    const auto newId = nextFreeId (project, ids::PATTERN);

    copy.setProperty (ids::id, newId, nullptr);

    // An auto-named pattern gets the next auto name; a renamed one keeps the
    // name it was given, marked as a copy, because that name is information.
    const auto sourceName = pattern[ids::name].toString();
    const auto autoName = "Pattern " + juce::String (sourceId);

    copy.setProperty (
        ids::name,
        sourceName == autoName ? "Pattern " + juce::String (newId) : sourceName + " copy", nullptr);

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

    const auto mixer = project.getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            owners.add (track);

    // The master carries a chain like any other bus, so it has to be counted
    // among the owners a new id is derived from. Left out, two effects added to
    // the master both took `highest + 1` and got the SAME id - and the engine
    // keys DSP state on the id, so they shared one module and fought over the
    // same reverb tank, which is exactly what unique ids exist to prevent.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
        owners.add (master);

    return owners;
}

juce::ValueTree ProjectEdits::oscillatorAt (const juce::ValueTree& channel, int index)
{
    if (index < 0)
        return {};

    int seen = 0;

    for (const auto& node : channel.getChildWithName (ids::INSTRUMENT))
        if (node.hasType (ids::OSC) && seen++ == index)
            return node;

    return {};
}

int ProjectEdits::countOscillators (const juce::ValueTree& channel)
{
    int count = 0;

    for (const auto& node : channel.getChildWithName (ids::INSTRUMENT))
        if (node.hasType (ids::OSC))
            ++count;

    return count;
}

juce::ValueTree ProjectEdits::addEffect (juce::ValueTree project, juce::ValueTree owner,
                                         const juce::String& type, juce::UndoManager* undo)
{
    if (! owner.isValid() || countEffects (owner) >= kMaxEffectsPerChain)
        return {};

    auto effect = defaultTreeFor (
        childSpecFor (childSpecFor (projectSpec(), "channels"), "effects"));
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

void ProjectEdits::removeEffect (juce::ValueTree owner, juce::ValueTree effect,
                                 juce::UndoManager* undo)
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

namespace
{

const NodeSpec& automationSpecFor()
{
    return childSpecFor (projectSpec(), "automations");
}

const NodeSpec& pointSpecFor()
{
    return childSpecFor (automationSpecFor(), "points");
}

/** Points in step order. The tree keeps them sorted, but a file could not, and
    evaluating an unsorted curve produces a shape nobody drew.
*/
juce::Array<juce::ValueTree> sortedPoints (const juce::ValueTree& automation)
{
    juce::Array<juce::ValueTree> points;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            points.add (child);

    std::stable_sort (points.begin(), points.end(),
                      [] (const juce::ValueTree& a, const juce::ValueTree& b)
                      { return (double) a[ids::step] < (double) b[ids::step]; });

    return points;
}

} // namespace

juce::ValueTree ProjectEdits::addAutomation (juce::ValueTree project,
                                             const AutomationTarget& target,
                                             juce::UndoManager* undo)
{
    auto automation = defaultTreeFor (automationSpecFor());

    automation.setProperty (ids::id, nextFreeId (project, ids::AUTOMATION), nullptr);
    automation.setProperty (ids::name, target.displayName, nullptr);
    automation.setProperty (ids::scope, automationScopeToString (target.scope), nullptr);
    automation.setProperty (ids::targetId, target.targetId, nullptr);
    automation.setProperty (ids::slot, target.slot, nullptr);
    automation.setProperty (ids::param, target.property.toString(), nullptr);

    // Two points, so a new clip is a line you can grab rather than an empty
    // rectangle that does nothing until you guess how to start it.
    //
    // Stepped when the target is discrete: a ramp between two states of a toggle
    // is a shape nobody meant to draw, and a bypass lane should look like a
    // bypass lane the moment it exists rather than after a trip to the menu.
    const auto discrete = target.spec != nullptr && target.spec->isDiscrete();

    for (const auto step : { 0.0, 16.0 })
    {
        auto point = defaultTreeFor (pointSpecFor());
        point.setProperty (ids::step, step, nullptr);
        point.setProperty (ids::value, 0.5, nullptr);

        if (discrete)
            point.setProperty (ids::shape, segmentShapeToString (SegmentShape::step), nullptr);

        automation.appendChild (point, nullptr);
    }

    int insertAt = project.getNumChildren();

    for (int i = 0; i < project.getNumChildren(); ++i)
        if (project.getChild (i).hasType (ids::PATTERN)
            || project.getChild (i).hasType (ids::AUTOMATION))
            insertAt = i + 1;

    project.addChild (automation, insertAt, undo);
    return automation;
}

juce::ValueTree ProjectEdits::addAutomationWithClip (juce::ValueTree project,
                                                     const AutomationTarget& target, int startBar,
                                                     int lengthBars, juce::UndoManager* undo)
{
    auto automation = addAutomation (project, target, undo);

    if (! automation.isValid())
        return {};

    auto playlist = project.getChildWithName (ids::PLAYLIST);

    for (auto track : playlist)
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        if (findClipAtBar (track, startBar).isValid())
            continue;

        auto clip = addAutomationClip (track, (int) automation[ids::id], startBar, lengthBars,
                                       undo);
        growSongToFitClips (project, undo);
        return clip;
    }

    // Every lane is taken at that bar, so make one. The alternative this used to
    // choose - undo the definition and return nothing - was a menu item that
    // did nothing at all, which is the worse of the two by a distance now that
    // every control offers it.
    auto track = addPlaylistTrack (project, "Track " + juce::String (playlist.getNumChildren() + 1),
                                   undo);

    if (! track.isValid())
        return {};

    auto clip = addAutomationClip (track, (int) automation[ids::id], startBar, lengthBars, undo);
    growSongToFitClips (project, undo);
    return clip;
}

juce::ValueTree ProjectEdits::findAutomation (const juce::ValueTree& project, int automationId)
{
    for (const auto& child : project)
        if (child.hasType (ids::AUTOMATION) && (int) child[ids::id] == automationId)
            return child;

    return {};
}

bool ProjectEdits::removeAutomation (juce::ValueTree project, juce::ValueTree automation,
                                     juce::UndoManager* undo)
{
    const auto index = project.indexOf (automation);

    if (index < 0)
        return false;

    const auto automationId = (int) automation[ids::id];

    // Same rule as removePattern: a clip referring to something that no longer
    // exists is dropped here, so the whole removal is one undo step.
    for (auto track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (int i = track.getNumChildren(); --i >= 0;)
        {
            const auto clip = track.getChild (i);

            if (clip.hasType (ids::CLIP) && isAutomationClip (clip)
                && (int) clip[ids::automationId] == automationId)
                track.removeChild (i, undo);
        }
    }

    project.removeChild (index, undo);
    return true;
}

juce::ValueTree ProjectEdits::addAutomationPoint (juce::ValueTree automation, double step,
                                                  double value, juce::UndoManager* undo)
{
    if (! automation.isValid())
        return {};

    const auto clampedStep = juce::jmax (0.0, step);
    const auto clampedValue = juce::jlimit (0.0, 1.0, value);

    // Two points on one step is a curve with no defined value there, so an
    // existing one moves instead of being joined.
    // The same gap moveAutomationPoint clamps to. Anything closer than that is
    // the same point: a pair inside the gap could never be separated again,
    // because the drag rule would not let either of them move past the other.
    for (auto existing : sortedPoints (automation))
        if (std::abs ((double) existing[ids::step] - clampedStep) < minPointGap)
        {
            existing.setProperty (ids::value, clampedValue, undo);
            return existing;
        }

    auto point = defaultTreeFor (pointSpecFor());
    point.setProperty (ids::step, clampedStep, nullptr);
    point.setProperty (ids::value, clampedValue, nullptr);

    // Inherit the shape of the segment being split, rather than taking the
    // default. A curve drawn as steps stays steps when a point is added to it;
    // otherwise adding one silently puts a ramp in the middle of a staircase.
    for (const auto& existing : sortedPoints (automation))
    {
        if ((double) existing[ids::step] > clampedStep)
            break;

        point.setProperty (ids::shape, existing[ids::shape], nullptr);
    }

    int insertAt = automation.getNumChildren();

    for (int i = 0; i < automation.getNumChildren(); ++i)
        if (automation.getChild (i).hasType (ids::POINT)
            && (double) automation.getChild (i)[ids::step] > clampedStep)
        {
            insertAt = i;
            break;
        }

    automation.addChild (point, insertAt, undo);
    return point;
}

void ProjectEdits::moveAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                        double step, double value, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // Clamped between the neighbours rather than let past them and re-sorted.
    //
    // Re-sorting worked, but it shuffled the tree under the point being dragged
    // - so a drag was a sequence of moveChild calls on the undo stack, and
    // anything holding the point's index had it change mid-gesture. Stopping the
    // point is also what the hand expects: a curve's points are in an order, and
    // a drag should meet that order rather than silently rewrite it.
    auto lower = 0.0;
    auto upper = std::numeric_limits<double>::max();

    const auto sorted = sortedPoints (automation);
    const auto index = sorted.indexOf (point);

    if (index > 0)
        lower = (double) sorted[index - 1][ids::step] + minPointGap;

    if (index >= 0 && index + 1 < sorted.size())
        upper = (double) sorted[index + 1][ids::step] - minPointGap;

    // A curve squeezed until its neighbours cross would otherwise invert the
    // range and jump the point to the far side of them.
    const auto clamped = upper > lower ? juce::jlimit (lower, upper, juce::jmax (0.0, step))
                                       : juce::jmax (0.0, lower);

    point.setProperty (ids::step, clamped, undo);
    point.setProperty (ids::value, juce::jlimit (0.0, 1.0, value), undo);
}

void ProjectEdits::removeAutomationPoint (juce::ValueTree automation, juce::ValueTree point,
                                          juce::UndoManager* undo)
{
    // A curve with fewer than two points has no shape to draw or evaluate.
    if (sortedPoints (automation).size() <= 2)
        return;

    const auto index = automation.indexOf (point);

    if (index >= 0)
        automation.removeChild (index, undo);
}

juce::Array<juce::ValueTree>
ProjectEdits::sortedAutomationPoints (const juce::ValueTree& automation)
{
    return sortedPoints (automation);
}

void ProjectEdits::setPointShape (juce::ValueTree point, SegmentShape shape,
                                  juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // The shape ONLY. A stepped segment ignores the bend rather than losing it,
    // so switching to step and back returns the curve you had - which is what
    // makes the three menu items reversible.
    point.setProperty (ids::shape, segmentShapeToString (shape), undo);
}

void ProjectEdits::setColour (juce::ValueTree node, const juce::String& hex,
                              juce::UndoManager* undo)
{
    if (! node.isValid())
        return;

    setProperty (node, ids::colour, hex, undo, "Change colour");
}

void ProjectEdits::setPointStraight (juce::ValueTree point, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    // What the editor's "Line" means, in one place and one undo step. A line is
    // a curve with no bend, so it is these two writes and not a third stored
    // shape - which would be a second place holding the same fact as
    // `curve == 0`, free to disagree with the bend beside it.
    point.setProperty (ids::shape, segmentShapeToString (SegmentShape::curve), undo);
    point.setProperty (ids::curve, 0.0, undo);
}

void ProjectEdits::setPointCurve (juce::ValueTree point, double bend, juce::UndoManager* undo)
{
    if (! point.isValid())
        return;

    point.setProperty (ids::curve, juce::jlimit (-1.0, 1.0, bend), undo);
}

double ProjectEdits::automationValueAt (const juce::ValueTree& automation, double step)
{
    // Delegates rather than evaluating. This body and AutomationSnapshot::valueAt
    // were the same arithmetic written twice, in two layers, and the playlist's
    // painter was a third place that implemented neither - so a bend was heard
    // and drawn straight, and a segment shape would have had to be added to
    // three places already free to disagree.
    const auto points = curvePointsOf (automation);

    return curveValueAt (points, step);
}

namespace
{

const NodeSpec& clipSpecFor()
{
    return childSpecFor (childSpecFor (childSpecFor (projectSpec(), "playlist"), "tracks"),
                         "clips");
}

} // namespace

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

    track.setProperty (ids::name, name.isNotEmpty() ? name : "Track " + juce::String (existing + 1),
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

juce::ValueTree ProjectEdits::addClip (juce::ValueTree playlistTrack, int patternId, int startBar,
                                       int lengthBars, juce::UndoManager* undo)
{
    // Built from the spec rather than by hand, so a property added to the
    // schema later is present here too. A hand-built node round-tripped into
    // something different from what the editor made, which the canonical-shape
    // test caught the moment clips gained a `kind`.
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "pattern", nullptr);
    clip.setProperty (ids::patternId, patternId, nullptr);
    clip.setProperty (ids::startBar, juce::jmax (0, startBar), nullptr);
    clip.setProperty (ids::lengthBars, juce::jmax (1, lengthBars), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

juce::ValueTree ProjectEdits::addAutomationClip (juce::ValueTree playlistTrack, int automationId,
                                                 int startBar, int lengthBars,
                                                 juce::UndoManager* undo)
{
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "automation", nullptr);
    clip.setProperty (ids::automationId, automationId, nullptr);
    clip.setProperty (ids::startBar, juce::jmax (0, startBar), nullptr);
    clip.setProperty (ids::lengthBars, juce::jmax (1, lengthBars), nullptr);

    playlistTrack.appendChild (clip, undo);
    return clip;
}

juce::ValueTree ProjectEdits::addAudioClip (juce::ValueTree playlistTrack, int channelId,
                                            int startBar, int lengthBars, juce::UndoManager* undo)
{
    auto clip = defaultTreeFor (clipSpecFor());
    clip.setProperty (ids::kind, "audio", nullptr);
    clip.setProperty (ids::channelId, channelId, nullptr);
    clip.setProperty (ids::startBar, juce::jmax (0, startBar), nullptr);
    clip.setProperty (ids::lengthBars, juce::jmax (1, lengthBars), nullptr);

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
                                        int startBar, juce::UndoManager* undo)
{
    if (! clip.isValid() || ! targetTrack.isValid())
        return {};

    // Whole-node rather than dispatching on the kind: a clip carries what it
    // refers to - a pattern, a channel or an automation lane - and copying the
    // node carries all three without this having to learn the table. Clips have
    // no id of their own, so nothing has to be reassigned.
    auto copy = clip.createCopy();
    copy.setProperty (ids::startBar, juce::jmax (0, startBar), nullptr);

    targetTrack.appendChild (copy, undo);
    return copy;
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

void ProjectEdits::setMeter (juce::ValueTree project, int beatsPerBar, int beatUnit,
                             juce::UndoManager* undo, bool* wasExact)
{
    if (wasExact != nullptr)
        *wasExact = true;

    if (! project.isValid())
        return;

    const auto before = Meter::of (project);

    Meter after = before;
    after.beatsPerBar = juce::jlimit (1, Meter::maxBeatsPerBar, beatsPerBar);
    after.beatUnit = Meter::clampBeatUnit (beatUnit);

    if (after == before)
        return;

    const auto oldStepsPerBar = before.stepsPerBar();
    const auto newStepsPerBar = after.stepsPerBar();

    project.setProperty (ids::beatsPerBar, after.beatsPerBar, undo);
    project.setProperty (ids::beatUnit, after.beatUnit, undo);

    // beatUnit alone is notational, so it moves no bar line and nothing below
    // needs doing.
    if (oldStepsPerBar == newStepsPerBar)
        return;

    // Bars in, steps out, bars back: the rescale is expressed as "what step was
    // this, and which bar is that now" rather than as a ratio, so there is one
    // place to read the intent and no ratio to get upside down.
    const auto barsForSteps = [newStepsPerBar] (int steps)
    { return juce::roundToInt ((double) steps / (double) newStepsPerBar); };

    auto exact = true;

    for (auto track : project.getChildWithName (ids::PLAYLIST))
    {
        if (! track.hasType (ids::PLAYLIST_TRACK))
            continue;

        for (auto clip : track)
        {
            if (! clip.hasType (ids::CLIP))
                continue;

            const auto startSteps = juce::jmax (0, (int) clip[ids::startBar]) * oldStepsPerBar;
            const auto lengthSteps = juce::jmax (1, (int) clip[ids::lengthBars]) * oldStepsPerBar;

            exact = exact && startSteps % newStepsPerBar == 0 && lengthSteps % newStepsPerBar == 0;

            clip.setProperty (ids::startBar, juce::jmax (0, barsForSteps (startSteps)), undo);
            clip.setProperty (ids::lengthBars, juce::jmax (1, barsForSteps (lengthSteps)), undo);
        }
    }

    // Ceiling rather than rounding, and then grown to fit: the song is a
    // container, and rounding it down would crop the arrangement it holds.
    const auto songSteps = juce::jmax (1, (int) project[ids::barsInSong]) * oldStepsPerBar;
    const auto songBars = (songSteps + newStepsPerBar - 1) / newStepsPerBar;

    project.setProperty (ids::barsInSong, juce::jmax (1, songBars), undo);
    growSongToFitClips (project, undo);

    if (wasExact != nullptr)
        *wasExact = exact;
}

// --- the score ---------------------------------------------------------------

void ProjectEdits::setScoreSource (juce::ValueTree project, const juce::String& text,
                                   const juce::String& sourceName, juce::UndoManager* undo)
{
    auto score = project.getChildWithName (ids::SCORE);

    if (! score.isValid())
    {
        score = juce::ValueTree (ids::SCORE);
        project.appendChild (score, undo);
    }

    score.setProperty (ids::name, sourceName, undo);

    while (score.getNumChildren() > 0)
        score.removeChild (score.getNumChildren() - 1, undo);

    // Split by hand rather than with StringArray::addLines, which drops the
    // empty string after a trailing newline. That empty line is real - it is
    // the difference between a file that ends in a newline and one that does
    // not - and losing it would make saving a score silently rewrite it.
    const auto normalised = text.replace ("\r\n", "\n").replace ("\r", "\n");

    // No text is no lines, not one empty one - so a project nobody has written
    // a score for is byte-identical to one whose score was cleared.
    if (normalised.isEmpty())
        return;

    auto start = 0;

    for (;;)
    {
        const auto end = normalised.indexOfChar (start, '\n');
        const auto line = end < 0 ? normalised.substring (start)
                                  : normalised.substring (start, end);

        juce::ValueTree node (ids::LINE);
        node.setProperty (ids::text, line, nullptr);
        score.appendChild (node, undo);

        if (end < 0)
            break;

        start = end + 1;
    }
}

juce::String ProjectEdits::scoreSource (const juce::ValueTree& project)
{
    const auto score = project.getChildWithName (ids::SCORE);

    if (! score.isValid())
        return {};

    juce::StringArray lines;

    for (const auto& line : score)
        if (line.hasType (ids::LINE))
            lines.add (line[ids::text].toString());

    return lines.joinIntoString ("\n");
}

juce::String ProjectEdits::scoreSourceName (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::SCORE)[ids::name].toString();
}

} // namespace dew

namespace dew
{

namespace
{

/** Writes one validated object onto one node, joining the open transaction.

    Every write after the first must join rather than open, or a preset would be
    a hundred undo steps. The transaction is opened by the caller and NOT by
    passing continuingTransaction=false for the first parameter: setProperty
    returns early when the value is already what it should be, before it opens
    anything, so a preset whose first parameter already matched would fold
    silently into whatever step was open.
*/
void writeParams (juce::ValueTree node, const juce::var& values, const ParamSpec* params,
                  int numParams, juce::UndoManager* undo, const juce::String& transactionName)
{
    auto* object = values.getDynamicObject();

    if (object == nullptr || ! node.isValid())
        return;

    for (int i = 0; i < numParams; ++i)
        ProjectEdits::setProperty (node, *params[i].property,
                                   object->getProperty (*params[i].property), undo, transactionName,
                                   /*continuingTransaction*/ true);
}

} // namespace

bool ProjectEdits::applyEffectPreset (juce::ValueTree effect, const Preset& preset,
                                      juce::UndoManager* undo)
{
    if (! effect.isValid() || ! effect.hasType (ids::EFFECT) || ! preset.isEffect())
        return false;

    const auto slotType = effectTypeFor (effect[ids::type].toString());
    const auto presetType = effectTypeFor (preset.typeId);

    if (! slotType.has_value() || ! presetType.has_value() || *slotType != *presetType)
        return false;

    juce::StringArray warnings;
    const auto& descriptor = effectDescriptor (*slotType);
    const auto values = validateState (descriptor, preset.state, warnings);

    const auto transactionName = "Load preset \"" + preset.name + "\"";

    if (undo != nullptr)
        undo->beginNewTransaction (transactionName);

    const auto params = effectParamsFor (*slotType);
    writeParams (effect, values, params.data(), (int) params.size(), undo, transactionName);

    return true;
}

bool ProjectEdits::applyInstrumentPreset (juce::ValueTree channel, const Preset& preset,
                                          juce::UndoManager* undo)
{
    if (! channel.isValid() || ! channel.hasType (ids::CHANNEL) || ! preset.isInstrument())
        return false;

    const auto channelType = instrumentTypeFor (channel[ids::source].toString());
    const auto presetType = instrumentTypeFor (preset.typeId);

    if (! channelType.has_value() || ! presetType.has_value() || *channelType != *presetType)
        return false;

    juce::StringArray warnings;
    const auto& descriptor = instrumentDescriptor (*channelType);
    const auto values = validateState (descriptor, preset.state, warnings);

    auto* object = values.getDynamicObject();

    if (object == nullptr)
        return false;

    const auto transactionName = "Load preset \"" + preset.name + "\"";

    if (undo != nullptr)
        undo->beginNewTransaction (transactionName);

    const auto instrument = channel.getChildWithName (ids::INSTRUMENT);

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];

        if (! group.inPreset)
            continue;

        const auto value = object->getProperty (juce::Identifier (group.jsonKey));
        const auto parent = (*group.node == ids::SAMPLE) ? channel : instrument;

        if (group.count <= 1)
        {
            writeParams (parent.getChildWithName (*group.node), value, group.params,
                         group.numParams, undo, transactionName);
            continue;
        }

        const auto* slots = value.getArray();

        if (slots == nullptr)
            continue;

        auto index = 0;

        for (const auto& child : parent)
        {
            if (! child.hasType (*group.node))
                continue;

            if (index >= slots->size())
                break;

            writeParams (child, (*slots)[index], group.params, group.numParams, undo,
                         transactionName);
            ++index;
        }
    }

    return true;
}

} // namespace dew
