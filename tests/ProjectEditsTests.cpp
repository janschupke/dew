#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "engine/EngineSnapshot.h"
#include "model/ProjectSerializer.h"

using namespace dew;

namespace
{

int countChildren (const juce::ValueTree& parent, const juce::Identifier& type)
{
    int count = 0;

    for (const auto& child : parent)
        if (child.hasType (type))
            ++count;

    return count;
}

} // namespace

TEST_CASE ("toggling a step adds a note, and toggling again removes it", "[edits]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    REQUIRE (pattern.isValid());
    REQUIRE (countChildren (pattern, ids::NOTE) == 0);

    REQUIRE (ProjectEdits::toggleStep (pattern, 1, 4, 60, &undo));
    REQUIRE (countChildren (pattern, ids::NOTE) == 1);

    REQUIRE (! ProjectEdits::toggleStep (pattern, 1, 4, 60, &undo));
    REQUIRE (countChildren (pattern, ids::NOTE) == 0);
}

TEST_CASE ("a step clears a note that is there at another pitch", "[edits]")
{
    // The grid shows "any note on this channel at this step". If toggling only
    // matched the base pitch, a note written in the piano roll would light a
    // cell that could not be switched off.
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    ProjectEdits::addNote (pattern, 1, 4, 2, 72, 1.0f, &undo);
    REQUIRE (countChildren (pattern, ids::NOTE) == 1);

    REQUIRE (! ProjectEdits::toggleStep (pattern, 1, 4, 60, &undo));
    REQUIRE (countChildren (pattern, ids::NOTE) == 0);
}

TEST_CASE ("steps on different channels and steps are independent", "[edits]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    ProjectEdits::toggleStep (pattern, 1, 0, 60, &undo);
    ProjectEdits::toggleStep (pattern, 2, 0, 60, &undo);
    ProjectEdits::toggleStep (pattern, 1, 1, 60, &undo);

    REQUIRE (countChildren (pattern, ids::NOTE) == 3);

    ProjectEdits::toggleStep (pattern, 1, 0, 60, &undo);
    REQUIRE (countChildren (pattern, ids::NOTE) == 2);
    REQUIRE (ProjectEdits::findNoteAtStep (pattern, 2, 0).isValid());
    REQUIRE (ProjectEdits::findNoteAtStep (pattern, 1, 1).isValid());
}

TEST_CASE ("every edit is undoable as a single step", "[edits][undo]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    undo.beginNewTransaction ("Add steps");

    for (int step = 0; step < 16; step += 4)
        ProjectEdits::toggleStep (pattern, 1, step, 60, &undo);

    REQUIRE (countChildren (pattern, ids::NOTE) == 4);

    // One transaction, so one undo takes back the whole drag.
    REQUIRE (undo.undo());
    REQUIRE (countChildren (pattern, ids::NOTE) == 0);

    REQUIRE (undo.redo());
    REQUIRE (countChildren (pattern, ids::NOTE) == 4);
}

TEST_CASE ("removing a channel removes the notes that referred to it", "[edits]")
{
    // A note pointing at a channel that no longer exists would be dropped by
    // the next snapshot with a warning. Removing them together keeps the
    // document consistent, and keeps it to one undo step.
    auto project = ProjectFactory::createDemo();
    auto pattern = ProjectEdits::findPattern (project, 1);
    auto channel = ProjectEdits::findChannel (project, 3);
    juce::UndoManager undo;

    REQUIRE (channel.isValid());

    int notesOnChannel3 = 0;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == 3)
            ++notesOnChannel3;

    REQUIRE (notesOnChannel3 > 0);

    const auto notesBefore = countChildren (pattern, ids::NOTE);

    undo.beginNewTransaction ("Remove channel");
    ProjectEdits::removeChannel (project, channel, &undo);

    REQUIRE (! ProjectEdits::findChannel (project, 3).isValid());
    REQUIRE (countChildren (pattern, ids::NOTE) == notesBefore - notesOnChannel3);

    // The snapshot must therefore have nothing to complain about.
    juce::StringArray warnings;
    buildSnapshot (project, &warnings);
    INFO ("warnings: " << warnings.joinIntoString ("; "));
    REQUIRE (warnings.isEmpty());

    REQUIRE (undo.undo());
    REQUIRE (ProjectEdits::findChannel (project, 3).isValid());
    REQUIRE (countChildren (pattern, ids::NOTE) == notesBefore);
}

TEST_CASE ("added channels and patterns get unused ids", "[edits]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    const auto channel = ProjectEdits::addChannel (project, "Extra", &undo);
    const auto pattern = ProjectEdits::addPattern (project, &undo);

    REQUIRE ((int) channel[ids::id] == 5);     // 1-4 already exist
    REQUIRE ((int) pattern[ids::id] == 2);

    // Ids must be unique, or notes and clips resolve to the wrong thing.
    juce::Array<int> channelIds;

    for (const auto& child : project)
        if (child.hasType (ids::CHANNEL))
        {
            REQUIRE (! channelIds.contains ((int) child[ids::id]));
            channelIds.add ((int) child[ids::id]);
        }
}

TEST_CASE ("editing keeps the tree in its canonical shape", "[edits][schema]")
{
    // Children are appended, so an edit could easily leave the tree in an order
    // that saving and loading would change. It must not.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    ProjectEdits::addChannel (project, "Extra", &undo);
    ProjectEdits::addPattern (project, &undo);

    auto pattern = ProjectEdits::findPattern (project, 1);
    ProjectEdits::toggleStep (pattern, 1, 0, 60, &undo);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    ProjectEdits::addClip (playlist.getChild (0), 1, 0, 2, &undo);

    REQUIRE (canonicalTree (project, projectSpec()).isEquivalentTo (project));

    const auto reloaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (reloaded.ok());
    REQUIRE (reloaded.warnings.isEmpty());
    REQUIRE (reloaded.tree.isEquivalentTo (project));
}

TEST_CASE ("clips are found by the bar they cover, not just where they start", "[edits]")
{
    auto project = ProjectFactory::createDefault();
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    juce::UndoManager undo;

    ProjectEdits::addClip (track, 1, 2, 3, &undo);   // bars 2, 3, 4

    REQUIRE (! ProjectEdits::findClipAtBar (track, 1).isValid());
    REQUIRE (ProjectEdits::findClipAtBar (track, 2).isValid());
    REQUIRE (ProjectEdits::findClipAtBar (track, 4).isValid());
    REQUIRE (! ProjectEdits::findClipAtBar (track, 5).isValid());
}

TEST_CASE ("edits refuse to produce nonsense values", "[edits]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    juce::UndoManager undo;

    const auto note = ProjectEdits::addNote (pattern, 1, -5, 0, 999, 4.0f, &undo);
    REQUIRE ((int) note[ids::step] == 0);
    REQUIRE ((int) note[ids::lengthSteps] == 1);
    REQUIRE ((int) note[ids::pitch] == 127);
    REQUIRE ((double) note[ids::velocity] <= 1.0);

    ProjectEdits::resizeNote (note, -3, &undo);
    REQUIRE ((int) note[ids::lengthSteps] == 1);

    ProjectEdits::moveNote (note, -10, -10, &undo);
    REQUIRE ((int) note[ids::step] == 0);
    REQUIRE ((int) note[ids::pitch] == 0);

    const auto clip = ProjectEdits::addClip (track, 1, -2, 0, &undo);
    REQUIRE ((int) clip[ids::startBar] == 0);
    REQUIRE ((int) clip[ids::lengthBars] == 1);
}
