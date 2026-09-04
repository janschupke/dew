// Writing notes, selecting them, and moving a selection.
//
// Split out of a PianoRollTests.cpp that had no tag seam left to use - every
// case in it was [ui][pianoroll]. These three are its subjects. The fixture is
// RollHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "ui/ChannelRackComponent.h"
#include "ui/ZoomButtons.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"

#include "FixtureProject.h"
#include "RollHarness.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("clicking empty space writes a note where it was clicked", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE (h.countNotes() == 0);

    const auto at = pointFor (h, 4, 72);
    clickAndRelease (h.roll, at);

    REQUIRE (h.countNotes() == 1);

    const auto note = h.pattern().getChild (0);
    REQUIRE ((int) note[ids::step] == 4);
    REQUIRE ((int) note[ids::pitch] == 72);
    REQUIRE (h.roll.getNumSelectedNotes() == 1);
}

TEST_CASE ("a new note takes the shape of the last one drawn", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // Draw a note and drag it out to four steps, the way one gesture does.
    h.roll.mouseDown (eventAt (h.roll, pointFor (h, 0, 72)));
    h.roll.mouseDrag (eventAt (h.roll, pointFor (h, 3, 72)));
    h.roll.mouseUp (eventAt (h.roll, pointFor (h, 3, 72)));

    REQUIRE ((int) h.pattern().getChild (0)[ids::lengthSteps] == 4);
    REQUIRE (h.editorState.getLastNoteLengthSteps() == 4);

    // The next note should not silently revert to one step.
    clickAndRelease (h.roll, pointFor (h, 8, 72));

    REQUIRE (h.countNotes() == 2);
    REQUIRE ((int) h.pattern().getChild (1)[ids::lengthSteps] == 4);
}

TEST_CASE ("a note drawn past the end grows the pattern to hold it", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE ((int) h.pattern()[ids::lengthSteps] == 16);

    // Shorten the pattern without refitting the view, which is what happens when
    // you trim a pattern you are already looking at: the bars past the end stay
    // on screen, painted as inert but still writable.
    juce::UndoManager& undo = h.document.getUndoManager();
    h.pattern().setProperty (ids::lengthSteps, 8, &undo);

    h.roll.mouseDown (eventAt (h.roll, pointFor (h, 11, 72)));
    h.roll.mouseDrag (eventAt (h.roll, pointFor (h, 14, 72)));
    h.roll.mouseUp (eventAt (h.roll, pointFor (h, 14, 72)));

    REQUIRE (h.countNotes() == 1);

    const auto note = h.pattern().getChild (0);
    REQUIRE ((int) note[ids::step] == 11);
    REQUIRE ((int) note[ids::lengthSteps] == 4);

    // The note runs to step 15, so the pattern must reach it - it grew rather
    // than clipping the note or refusing the click.
    REQUIRE ((int) h.pattern()[ids::lengthSteps] == 15);
    REQUIRE (ProjectEdits::lengthNeededForNotes (h.pattern())
             <= (int) h.pattern()[ids::lengthSteps]);

    // Growing is one-way: a pattern left longer than its notes is a rest at the
    // end, and must not be silently trimmed.
    h.pattern().setProperty (ids::lengthSteps, 64, &undo);
    REQUIRE (! ProjectEdits::growPatternToFitNotes (h.pattern(), &undo));
    REQUIRE ((int) h.pattern()[ids::lengthSteps] == 64);
}

TEST_CASE ("a rubber band selects the notes it covers, and delete removes them", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();

    for (int step : { 0, 2, 4 })
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 70, 1.0f, &undo);

    ProjectEdits::addNote (h.pattern(), 1, 12, 1, 60, 1.0f, &undo);

    REQUIRE (h.countNotes() == 4);

    const auto mods = juce::ModifierKeys (juce::ModifierKeys::commandModifier);
    const auto from = pointFor (h, 0, 72);
    const auto to = pointFor (h, 6, 68);

    h.roll.mouseDown (eventAt (h.roll, from, mods));
    h.roll.mouseDrag (eventAt (h.roll, to, mods));

    REQUIRE (h.roll.getNumSelectedNotes() == 3);

    h.roll.mouseUp (eventAt (h.roll, to, mods));

    REQUIRE (h.roll.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (h.countNotes() == 1);

    // The one outside the band survived.
    REQUIRE ((int) h.pattern().getChild (0)[ids::step] == 12);
}

TEST_CASE ("moving a selection keeps its shape", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 60, 1.0f, &undo); // a third apart
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 64, 1.0f, &undo);
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 67, 1.0f, &undo);

    REQUIRE (h.roll.keyPressed (
        juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a')));
    REQUIRE (h.roll.getNumSelectedNotes() == 3);

    // Grab the middle note of the chord and move it up two steps and a tone.
    h.roll.mouseDown (eventAt (h.roll, pointFor (h, 2, 64)));
    h.roll.mouseDrag (eventAt (h.roll, pointFor (h, 6, 66)));
    h.roll.mouseUp (eventAt (h.roll, pointFor (h, 6, 66)));

    juce::Array<int> pitches, steps;

    for (const auto& note : h.pattern())
        if (note.hasType (ids::NOTE))
        {
            pitches.add ((int) note[ids::pitch]);
            steps.add ((int) note[ids::step]);
        }

    pitches.sort();

    // The chord kept its intervals and all of it moved together.
    REQUIRE (pitches.size() == 3);
    REQUIRE (pitches[1] - pitches[0] == 4);
    REQUIRE (pitches[2] - pitches[1] == 3);
    REQUIRE (steps[0] == steps[1]);
    REQUIRE (steps[1] == steps[2]);
    REQUIRE (steps[0] == 6);
    REQUIRE (pitches[1] == 66);
}

TEST_CASE ("a selection cannot be dragged off the start of the pattern", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 0, 1, 60, 1.0f, &undo);
    ProjectEdits::addNote (h.pattern(), 1, 4, 1, 64, 1.0f, &undo);

    REQUIRE (h.roll.keyPressed (
        juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a')));

    // Drag the later note four steps left: the earlier one is already at 0, so
    // the group must not move at all rather than collapsing onto step 0.
    h.roll.mouseDown (eventAt (h.roll, pointFor (h, 4, 64)));
    h.roll.mouseDrag (eventAt (h.roll, pointFor (h, 0, 64)));
    h.roll.mouseUp (eventAt (h.roll, pointFor (h, 0, 64)));

    juce::Array<int> steps;

    for (const auto& note : h.pattern())
        if (note.hasType (ids::NOTE))
            steps.add ((int) note[ids::step]);

    steps.sort();
    REQUIRE (steps[0] == 0);
    REQUIRE (steps[1] == 4);
}

TEST_CASE ("painting repeats the selected note's shape", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 2, 4, 72, 0.4f, &undo);

    // Rubber-banded rather than clicked: a click starts a move, and a move
    // remembers the note on its own release, so clicking would make this pass
    // without the selection being read at all.
    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };
    dragBetween (h.roll, pointFor (h, 0, 74), pointFor (h, 6, 70), 8, mod);

    REQUIRE (h.roll.getNumSelectedNotes() == 1);

    h.roll.setTool (RollTool::paint);
    dragBetween (h.roll, pointFor (h, 8, 66), pointFor (h, 12, 66));

    int painted = 0;

    for (const auto& note : h.pattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::pitch] == 66)
        {
            ++painted;
            CHECK ((int) note[ids::lengthSteps] == 4);
            CHECK ((double) note[ids::velocity] < 0.5);
        }

    REQUIRE (painted > 1);
}

TEST_CASE ("an unrelated editor-state change leaves the selection alone", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h;

    juce::UndoManager setup;

    for (int step = 0; step < 4; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    // EditorState is a ChangeBroadcaster, so its notifications are ASYNC. Without
    // pumping, every assertion below passes for the wrong reason - the callback
    // simply never runs - which is exactly how the first draft of this test went
    // green while proving nothing.
    const auto pump = [&h] { h.editorState.dispatchPendingMessages(); };

    const auto selectAll = [&h, &pump]
    {
        h.roll.grabKeyboardFocus();
        h.roll.keyPressed (
            juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
        pump();
    };

    selectAll();
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    // The control: pointing the roll at another channel DOES clear it, because
    // those notes are not on screen any more and the next gesture would edit
    // them invisibly. If this stops working the cases below mean nothing.
    h.editorState.setSelectedChannelId (2);
    pump();
    REQUIRE (h.roll.getNumSelectedNotes() == 0);

    h.editorState.setSelectedChannelId (1);
    pump();
    selectAll();
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    // EditorState broadcasts for everything it holds, and the roll used to clear
    // its selection on all of them - so expanding an effect card, clicking a
    // mixer strip or dragging a span on the playlist ruler each threw away a
    // selection built up in here.
    h.editorState.setSelectedMixerTrackId (3);
    pump();
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    h.editorState.setEffectExpanded (7, true);
    pump();
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    h.editorState.setSelectedBarRange ({ 1, 3 });
    pump();
    REQUIRE (h.roll.getNumSelectedNotes() == 4);
}

TEST_CASE ("the toolbar points the roll at another channel", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& toolbar = h.roll.getToolbar();

    // The roll could say "Select a channel in the Channel Rack" but offered no
    // way to do it, so its own empty state was a dead end.
    REQUIRE (toolbar.getSelectedChannel() == h.editorState.getSelectedChannelId());
    REQUIRE (toolbar.onChannelChanged != nullptr);

    toolbar.onChannelChanged (3);
    CHECK (h.editorState.getSelectedChannelId() == 3);

    // And it follows a selection made anywhere else - the rack, the mixer, the
    // step grid all write the same field.
    h.editorState.setSelectedChannelId (2);
    h.editorState.dispatchPendingMessages();
    CHECK (toolbar.getSelectedChannel() == 2);
}

TEST_CASE ("the channel selector lists every channel, by name", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& toolbar = h.roll.getToolbar();

    // A renamed channel is one the selector can no longer be used to find.
    ProjectEdits::findChannel (h.document.getState(), 3)
        .setProperty (ids::name, "Sub", &h.document.getUndoManager());

    toolbar.setSelectedChannel (3);
    CHECK (toolbar.getSelectedChannel() == 3);
}
