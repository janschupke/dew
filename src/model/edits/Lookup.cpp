// =============================================================================
// Finding a node, and the two generic writers.
//
// One of seven translation units behind model/ProjectEdits.h. The header is
// one struct of static functions and stays where it was; this directory is
// where they are defined.
//
// Everything else here calls one of these. The finders answer by id,
// which is what every editor holds rather than a tree; setProperty is
// the one scalar write in the application, and setColour the one that
// knows a colour is stored as a hex string and absent means inherit.
// =============================================================================

#include "model/ProjectEdits.h"

#include "model/Constants.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"
#include "model/edits/FindChild.h"

namespace dew
{

juce::ValueTree ProjectEdits::findChannel (const juce::ValueTree& project, int channelId)
{
    return edits::findChildWithId (project, ids::CHANNEL, channelId);
}

juce::ValueTree ProjectEdits::findPattern (const juce::ValueTree& project, int patternId)
{
    return edits::findChildWithId (project, ids::PATTERN, patternId);
}

juce::ValueTree ProjectEdits::findMixerTrack (const juce::ValueTree& project, int mixerTrackId)
{
    return edits::findChildWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK,
                                   mixerTrackId);
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
    note.setProperty (ids::velocity, juce::jlimit (kMinNoteVelocity, 1.0, (double) velocity),
                      nullptr);

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

void ProjectEdits::setPropertyOnEvery (juce::ValueTree parent, const juce::Identifier& type,
                                       const juce::Identifier& property, const juce::var& value,
                                       juce::UndoManager* undo, const juce::String& transactionName)
{
    if (! parent.isValid())
        return;

    // The transaction is opened HERE rather than left to the first write, and
    // that is the whole reason this is a function. setProperty skips a write of
    // the value already there - which is right, and means the first child that
    // needs changing is not always the first child. Opening on the first WRITE
    // would leave the step named after whichever track happened to differ, and
    // opening one per write would make "silence every track" eight undo steps.
    if (undo != nullptr)
        undo->beginNewTransaction (transactionName);

    for (auto child : parent)
        if (child.hasType (type))
            setProperty (child, property, value, undo, transactionName,
                         /*continuingTransaction*/ true);
}

void ProjectEdits::setNoteVelocity (juce::ValueTree note, double velocity, juce::UndoManager* undo)
{
    note.setProperty (ids::velocity, juce::jlimit (kMinNoteVelocity, 1.0, velocity), undo);
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

} // namespace dew
