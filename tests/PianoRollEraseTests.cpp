// The erase sweep, and what a right-press means when it erases nothing.
//
// Split out of a PianoRollTests.cpp that was 1,369 lines, along the Catch2
// tags it already carried. The fixture is RollHarness.h, which was already
// shared.

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

TEST_CASE ("a right-drag sweeps notes away, as one undo step", "[pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h;

    juce::UndoManager setup;

    // A run of notes along one pitch, so a single horizontal sweep crosses all
    // of them.
    for (int step = 0; step < 8; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    const auto notesBefore = h.countNotes();
    REQUIRE (notesBefore >= 8);

    const auto from = pointFor (h, 0, 66);
    const auto to = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp (eventAt (h.roll, to, rightButton));

    INFO ("notes left after the sweep: " << h.countNotes());
    REQUIRE (h.countNotes() == notesBefore - 8);

    // One transaction for the whole gesture. It used to open one per note, so
    // clearing a bar cost a bar's worth of undo presses.
    REQUIRE (h.document.getUndoManager().undo());
    REQUIRE (h.countNotes() == notesBefore);
}

TEST_CASE ("a fast sweep does not step over notes between drag samples", "[pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h;

    juce::UndoManager setup;

    for (int step = 0; step < 8; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    const auto notesBefore = h.countNotes();

    // Two samples for the whole sweep, which is what a quick flick actually
    // produces. Only sampling where the pointer was reported would leave the
    // six notes in between untouched.
    const auto from = pointFor (h, 0, 66);
    const auto to = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp (eventAt (h.roll, to, rightButton));

    INFO ("notes left after a two-sample sweep: " << h.countNotes());
    REQUIRE (h.countNotes() == notesBefore - 8);
}

TEST_CASE ("a sweep can start on empty space and run into notes", "[pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h;

    juce::UndoManager setup;

    for (int step = 4; step < 8; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    const auto notesBefore = h.countNotes();

    // Step 0 is empty. Pressing there used to do nothing at all, so there was
    // no way to begin a sweep anywhere but exactly on a note.
    const auto from = pointFor (h, 0, 66);
    const auto to = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp (eventAt (h.roll, to, rightButton));

    REQUIRE (h.countNotes() == notesBefore - 4);
}

TEST_CASE ("alt-drag erases too, and a plain drag still does not", "[pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h;

    juce::UndoManager setup;

    for (int step = 0; step < 6; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    const auto notesBefore = h.countNotes();
    const auto from = pointFor (h, 0, 66);
    const auto to = pointFor (h, 5, 66);

    SECTION ("alt")
    {
        const juce::ModifierKeys alt { juce::ModifierKeys::altModifier };

        h.roll.mouseDown (eventAt (h.roll, from, alt));
        h.roll.mouseDrag (eventAt (h.roll, to, alt));
        h.roll.mouseUp (eventAt (h.roll, to, alt));

        REQUIRE (h.countNotes() == notesBefore - 6);
    }

    SECTION ("plain left drag moves rather than erasing")
    {
        const juce::ModifierKeys left { juce::ModifierKeys::leftButtonModifier };

        h.roll.mouseDown (eventAt (h.roll, from, left));
        h.roll.mouseDrag (eventAt (h.roll, to, left));
        h.roll.mouseUp (eventAt (h.roll, to, left));

        REQUIRE (h.countNotes() == notesBefore);
    }
}

TEST_CASE ("right-clicking empty space clears the note selection", "[ui][pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager setup;

    for (int step = 0; step < 4; ++step)
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);

    h.roll.zoomToFit();
    h.roll.refresh();

    h.roll.grabKeyboardFocus();
    h.roll.keyPressed (
        juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    const auto notesBefore = h.countNotes();

    // Empty space: a pitch nothing was written at. A right-press here starts an
    // erase sweep - that is how you sweep INTO notes - but a press that lets go
    // having removed nothing was never an erase.
    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };
    const auto empty = pointFor (h, 0, 72);

    h.roll.mouseDown (eventAt (h.roll, empty, rightButton));
    h.roll.mouseUp (eventAt (h.roll, empty, rightButton));

    REQUIRE (h.roll.getNumSelectedNotes() == 0);
    REQUIRE (h.countNotes() == notesBefore);
}

TEST_CASE ("a right-drag that erases does not also clear the selection", "[ui][pianoroll][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager setup;

    // Two rows: one to erase, one to keep selected.
    for (int step = 0; step < 4; ++step)
    {
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 66, 1.0f, &setup);
        ProjectEdits::addNote (h.pattern(), 1, step, 1, 72, 1.0f, &setup);
    }

    h.roll.zoomToFit();
    h.roll.refresh();

    h.roll.grabKeyboardFocus();
    h.roll.keyPressed (
        juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
    REQUIRE (h.roll.getNumSelectedNotes() == 8);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    dragBetween (h.roll, pointFor (h, 0, 66), pointFor (h, 3, 66), 8, rightButton);

    // The four it swept are gone, and gone from the selection with them - but
    // the sweep must not clear the four on the other row as well.
    REQUIRE (h.countNotes() == 4);
    REQUIRE (h.roll.getNumSelectedNotes() == 4);
}
