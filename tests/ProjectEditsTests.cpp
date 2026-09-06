#include <catch2/catch_test_macros.hpp>

#include "model/Constants.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "engine/EngineSnapshot.h"
#include "model/ProjectSerializer.h"
#include "FixtureProject.h"

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
    auto project = dew::testing::fixtureProject();
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

    REQUIRE ((int) channel[ids::id] == 5); // 1-4 already exist
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

    ProjectEdits::addClip (track, 1, 2, 3, &undo); // bars 2, 3, 4

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
    REQUIRE (juce::exactlyEqual ((double) note[ids::velocity], 1.0));

    // Both ends, and the floor is the interesting one: addNote was the only
    // one of the four velocity writers that allowed zero, so a note added
    // through it existed, drew, and could not be heard. setNoteVelocity, the
    // audition and the drawn-note memory all floored it; this one did not.
    const auto silent = ProjectEdits::addNote (pattern, 1, 0, 1, 60, 0.0f, &undo);
    REQUIRE (juce::exactlyEqual ((double) silent[ids::velocity], kMinNoteVelocity));

    ProjectEdits::setNoteVelocity (silent, 0.0, &undo);
    REQUIRE (juce::exactlyEqual ((double) silent[ids::velocity], kMinNoteVelocity));

    ProjectEdits::resizeNote (note, -3, &undo);
    REQUIRE ((int) note[ids::lengthSteps] == 1);

    ProjectEdits::moveNote (note, -10, -10, &undo);
    REQUIRE ((int) note[ids::step] == 0);
    REQUIRE ((int) note[ids::pitch] == 0);

    const auto clip = ProjectEdits::addClip (track, 1, -2, 0, &undo);
    REQUIRE ((int) clip[ids::startBar] == 0);
    REQUIRE ((int) clip[ids::lengthBars] == 1);
}

TEST_CASE ("duplicating a pattern copies its notes under a new identity", "[edits][patterns]")
{
    auto project = ProjectFactory::createDefault();
    auto source = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    ProjectEdits::toggleStep (source, 1, 0, 60, &undo);
    ProjectEdits::toggleStep (source, 1, 4, 67, &undo);
    source.setProperty (ids::lengthSteps, 32, &undo);

    const auto copy = ProjectEdits::duplicatePattern (project, source, &undo);

    REQUIRE (copy.isValid());
    REQUIRE ((int) copy[ids::id] != (int) source[ids::id]);
    REQUIRE (countChildren (copy, ids::NOTE) == 2);
    REQUIRE ((int) copy[ids::lengthSteps] == 32);

    // A deep copy, not a shared reference: editing the copy must not reach back.
    auto mutableCopy = copy;
    ProjectEdits::toggleStep (mutableCopy, 1, 8, 72, &undo);
    REQUIRE (countChildren (copy, ids::NOTE) == 3);
    REQUIRE (countChildren (source, ids::NOTE) == 2);

    // It lands next to the original rather than at the end of the list.
    REQUIRE (project.indexOf (copy) == project.indexOf (source) + 1);

    // An auto-named pattern gets the next auto name; a renamed one is marked.
    REQUIRE (copy[ids::name].toString() == "Pattern " + juce::String ((int) copy[ids::id]));

    source.setProperty (ids::name, "Drums", &undo);
    const auto second = ProjectEdits::duplicatePattern (project, source, &undo);
    REQUIRE (second[ids::name].toString() == "Drums copy");
}

TEST_CASE ("removing a pattern removes the clips that referred to it", "[edits][patterns]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    const auto first = ProjectEdits::findPattern (project, 1);
    const auto second = ProjectEdits::addPattern (project, &undo);
    const auto secondId = (int) second[ids::id];

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto track = playlist.getChild (0);
    REQUIRE (track.hasType (ids::PLAYLIST_TRACK));

    ProjectEdits::addClip (track, (int) first[ids::id], 0, 1, &undo);
    ProjectEdits::addClip (track, secondId, 1, 1, &undo);
    ProjectEdits::addClip (track, secondId, 2, 1, &undo);

    const auto clipsBefore = countChildren (track, ids::CLIP);

    REQUIRE (ProjectEdits::removePattern (project, second, &undo));
    REQUIRE (countChildren (project, ids::PATTERN) == 1);

    // Both clips that pointed at it go; the one that did not, stays.
    REQUIRE (countChildren (track, ids::CLIP) == clipsBefore - 2);

    for (const auto& clip : track)
        if (clip.hasType (ids::CLIP))
            REQUIRE ((int) clip[ids::patternId] != secondId);
}

TEST_CASE ("the last pattern cannot be removed", "[edits][patterns]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    REQUIRE (countChildren (project, ids::PATTERN) == 1);
    REQUIRE (
        ! ProjectEdits::removePattern (project, ProjectEdits::findPattern (project, 1), &undo));
    REQUIRE (countChildren (project, ids::PATTERN) == 1);
}

TEST_CASE ("pattern deletion is one undo step", "[edits][patterns][undo]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    const auto second = ProjectEdits::addPattern (project, &undo);
    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto track = playlist.getChild (0);
    ProjectEdits::addClip (track, (int) second[ids::id], 0, 1, &undo);

    const auto patterns = countChildren (project, ids::PATTERN);
    const auto clips = countChildren (track, ids::CLIP);

    undo.beginNewTransaction ("Delete pattern");
    REQUIRE (ProjectEdits::removePattern (project, second, &undo));
    REQUIRE (undo.undo());

    REQUIRE (countChildren (project, ids::PATTERN) == patterns);
    REQUIRE (countChildren (track, ids::CLIP) == clips);
}

TEST_CASE ("a pattern length that fits its notes covers the last one entirely", "[edits][patterns]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    constexpr auto perBar = 16;

    // Empty patterns still need somewhere to put a note, and the smallest thing
    // a pattern can be is one bar.
    REQUIRE (ProjectEdits::lengthNeededForNotes (pattern, perBar) == perBar);

    ProjectEdits::addNote (pattern, 1, 4, 1, 60, 1.0f, &undo);
    REQUIRE (ProjectEdits::lengthNeededForNotes (pattern, perBar) == perBar);

    // The length of the last note counts, not just where it starts, and an
    // earlier long note can outreach a later short one - so this reaches step
    // 20 and takes a second bar with it.
    ProjectEdits::addNote (pattern, 1, 8, 12, 62, 1.0f, &undo);
    ProjectEdits::addNote (pattern, 1, 15, 1, 64, 1.0f, &undo);
    REQUIRE (ProjectEdits::lengthNeededForNotes (pattern, perBar) == 2 * perBar);

    // Whole bars, and only whole bars. A note ending one step into a bar takes
    // the whole of it, because a pattern that ended part way through one would
    // wrap the arrangement where no bar line is.
    ProjectEdits::addNote (pattern, 1, 32, 1, 66, 1.0f, &undo);
    REQUIRE (ProjectEdits::lengthNeededForNotes (pattern, perBar) == 3 * perBar);
}

TEST_CASE ("a pattern's length follows the notes in BOTH directions", "[edits][patterns]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    constexpr auto perBar = 16;

    auto far = ProjectEdits::addNote (pattern, 1, 40, 4, 60, 1.0f, &undo);
    ProjectEdits::addNote (pattern, 1, 2, 1, 62, 1.0f, &undo);

    REQUIRE (ProjectEdits::fitPatternToNotes (pattern, perBar, &undo));
    REQUIRE ((int) pattern[ids::lengthSteps] == 3 * perBar);

    // The whole point of the change: taking the last bar's notes away takes the
    // bar away too, so what loops is what is there rather than what was ever
    // there. The old rule grew only, and a pattern that had once reached bar
    // three went on looping three bars of silence after two of them were
    // emptied.
    ProjectEdits::removeNote (pattern, far, &undo);

    REQUIRE (ProjectEdits::fitPatternToNotes (pattern, perBar, &undo));
    REQUIRE ((int) pattern[ids::lengthSteps] == perBar);

    // Idempotent: a second fit over the same notes changes nothing and says so,
    // which is what keeps it out of the undo stack.
    REQUIRE (! ProjectEdits::fitPatternToNotes (pattern, perBar, &undo));

    // An empty pattern is one bar, not one step.
    ProjectEdits::removeNote (pattern, pattern.getChild (0), &undo);
    ProjectEdits::fitPatternToNotes (pattern, perBar, &undo);
    REQUIRE ((int) pattern[ids::lengthSteps] == perBar);
}

TEST_CASE ("a clip can be moved to another track", "[edits][playlist]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto first = playlist.getChild (0);
    auto second = playlist.getChild (1);

    REQUIRE (first.hasType (ids::PLAYLIST_TRACK));
    REQUIRE (second.hasType (ids::PLAYLIST_TRACK));

    auto clip = ProjectEdits::addClip (first, 1, 0, 2, &undo);
    REQUIRE (countChildren (first, ids::CLIP) == 1);

    undo.beginNewTransaction ("Move clip to track");
    auto moved = ProjectEdits::moveClipToTrack (first, clip, second, 5, &undo);

    REQUIRE (countChildren (first, ids::CLIP) == 0);
    REQUIRE (countChildren (second, ids::CLIP) == 1);

    // It kept everything except where it is.
    REQUIRE ((int) moved[ids::patternId] == 1);
    REQUIRE ((int) moved[ids::lengthBars] == 2);
    REQUIRE ((int) moved[ids::startBar] == 5);

    // And crossing tracks is a single undo step, not two.
    REQUIRE (undo.undo());
    REQUIRE (countChildren (first, ids::CLIP) == 1);
    REQUIRE (countChildren (second, ids::CLIP) == 0);
}

TEST_CASE ("moving a clip onto its own track is an ordinary move", "[edits][playlist]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    auto clip = ProjectEdits::addClip (track, 1, 0, 1, &undo);

    const auto same = ProjectEdits::moveClipToTrack (track, clip, track, 3, &undo);

    REQUIRE (same == clip);
    REQUIRE ((int) clip[ids::startBar] == 3);
    REQUIRE (countChildren (track, ids::CLIP) == 1);
}

TEST_CASE ("a playlist track can be added and removed", "[edits][playlist]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    const auto countTracks = [&project]
    {
        int n = 0;

        for (const auto& track : project.getChildWithName (ids::PLAYLIST))
            if (track.hasType (ids::PLAYLIST_TRACK))
                ++n;

        return n;
    };

    const auto before = countTracks();
    REQUIRE (before == 4);

    undo.beginNewTransaction();
    auto added = ProjectEdits::addPlaylistTrack (project, {}, &undo);

    REQUIRE (added.isValid());
    REQUIRE (countTracks() == before + 1);
    REQUIRE (added[ids::name].toString() == "Track 5");

    // Built from the spec, so it is shaped exactly like the four the factory
    // made - a hand-built node stops round-tripping the moment the schema grows.
    REQUIRE (canonicalTree (project, projectSpec()).isEquivalentTo (project));

    undo.beginNewTransaction();
    ProjectEdits::removePlaylistTrack (project, added, &undo);
    REQUIRE (countTracks() == before);

    REQUIRE (undo.undo());
    REQUIRE (countTracks() == before + 1);
}

TEST_CASE ("removing a playlist track removes its clips in one undo step",
           "[edits][playlist][undo]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto track = playlist.getChild (0);
    REQUIRE (track.hasType (ids::PLAYLIST_TRACK));

    ProjectEdits::addClip (track, 1, 0, 2, nullptr);
    ProjectEdits::addClip (track, 1, 4, 2, nullptr);

    const auto countClips = [&track]
    {
        int n = 0;

        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                ++n;

        return n;
    };

    REQUIRE (countClips() == 2);

    undo.beginNewTransaction ("Remove track");
    ProjectEdits::removePlaylistTrack (project, track, &undo);

    // The clips are children, so they left with it - and one undo brings the
    // track and both clips back together, not the track and then the clips.
    REQUIRE (undo.undo());
    REQUIRE (countClips() == 2);
}

TEST_CASE ("a whole gesture is one undo step", "[model][undo]")
{
    // The rule that was copy-pasted into five components and missing from a
    // sixth: a drag emits a value per frame, so without it, dragging a knob
    // across its range makes a hundred undo steps and getting back to where you
    // started means pressing undo a hundred times.
    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto& undo = document.getUndoManager();
    auto channel = document.getState().getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    const auto before = (double) channel[ids::volume];

    // Twenty values, the way a drag arrives: the first opens the transaction
    // and the rest join it.
    for (int i = 1; i <= 20; ++i)
        ProjectEdits::setProperty (channel, ids::volume, before * 0.5 + (double) i * 0.01, &undo,
                                   "Change volume", i > 1);

    REQUIRE (! juce::exactlyEqual ((double) channel[ids::volume], before));

    REQUIRE (undo.undo());
    CHECK (juce::exactlyEqual ((double) channel[ids::volume], before));

    // And nothing else is left behind it, which is what "one step" means.
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("separate gestures are separate undo steps", "[model][undo]")
{
    // The other half. Coalescing everything into one transaction would make a
    // whole session's worth of edits undo in a single press.
    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto& undo = document.getUndoManager();
    auto channel = document.getState().getChildWithName (ids::CHANNEL);

    const auto before = (double) channel[ids::volume];

    ProjectEdits::setProperty (channel, ids::volume, 0.25, &undo, "Change volume", false);
    ProjectEdits::setProperty (channel, ids::volume, 0.75, &undo, "Change volume", false);

    REQUIRE (undo.undo());
    CHECK (juce::exactlyEqual ((double) channel[ids::volume], 0.25));

    REQUIRE (undo.undo());
    CHECK (juce::exactlyEqual ((double) channel[ids::volume], before));
}

TEST_CASE ("writing the value that is already there records nothing", "[model][undo]")
{
    // Four components re-state every control on every document change. A
    // ValueTree write of the value already present still opens a transaction
    // and still pushes an undo step, which is how a refresh() ends up in the
    // undo history as an edit nobody made.
    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    auto& undo = document.getUndoManager();
    auto channel = document.getState().getChildWithName (ids::CHANNEL);

    ProjectEdits::setProperty (channel, ids::volume, channel[ids::volume], &undo, "Change volume",
                               false);

    CHECK_FALSE (undo.canUndo());
}
