#include <catch2/catch_test_macros.hpp>

#include "RollHarness.h"
#include "app/Settings.h"
#include "model/NoteTools.h"
#include "ui/RandomizePanel.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** The notes of channel 1, sorted by step, so an assertion can talk about "the
    third note" without depending on the order the tree happens to hold.
*/
juce::Array<juce::ValueTree> sortedNotes (RollHarness& h)
{
    auto notes = NoteTools::notesOnChannel (h.pattern(), 1);

    std::sort (notes.begin(), notes.end(),
               [] (const juce::ValueTree& a, const juce::ValueTree& b)
               {
                   if ((int) a[ids::step] != (int) b[ids::step])
                       return (int) a[ids::step] < (int) b[ids::step];

                   return (int) a[ids::pitch] < (int) b[ids::pitch];
               });

    return notes;
}

} // namespace

// --- layout ------------------------------------------------------------------

TEST_CASE ("the grid starts below the tool strip, and the areas still stack", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto ruler = h.roll.getRulerArea();
    const auto notes = h.roll.getNoteArea();
    const auto keys = h.roll.getKeyboardArea();
    const auto lane = h.roll.getVelocityArea();

    // The toolbar is a real strip, not a zero-height placeholder.
    REQUIRE (ruler.getY() > 0);

    CHECK (notes.getY() == ruler.getBottom());
    CHECK (keys.getY() == notes.getY());
    CHECK (keys.getHeight() == notes.getHeight());
    CHECK (lane.getY() >= notes.getBottom());
    CHECK (lane.getBottom() <= h.roll.getHeight());
}

TEST_CASE ("a note is painted where the grid says it is, with the strip in place",
           "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // pointFor REQUIREs the point it returns is inside the note area, so this
    // fails loudly if the strip ever shifts painting and hit-testing apart.
    const auto point = pointFor (h, 6, 66);
    clickAndRelease (h.roll, point);

    REQUIRE (h.countNotes() == 1);
    CHECK (h.roll.getBoundsForNote (h.pattern().getChild (0)).contains (point.toFloat()));
}

// --- snapping ----------------------------------------------------------------

TEST_CASE ("at a coarse grid a note is written at the start of the cell clicked", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.setSnap (SnapDivision::quarter); // four steps per cell

    clickAndRelease (h.roll, pointFor (h, 6, 66));

    REQUIRE (h.countNotes() == 1);

    const auto note = h.pattern().getChild (0);
    CHECK ((int) note[ids::step] == 4);

    // And it fills the cell rather than being a one-step stub on a 1/4 grid.
    CHECK ((int) note[ids::lengthSteps] == 4);
}

TEST_CASE ("the finest grid leaves the original behaviour exactly as it was", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE (h.roll.getSnap() == SnapDivision::sixteenth);

    clickAndRelease (h.roll, pointFor (h, 5, 66));

    REQUIRE (h.countNotes() == 1);
    CHECK ((int) h.pattern().getChild (0)[ids::step] == 5);
}

TEST_CASE ("a dragged chord keeps its offsets while the grabbed note lands on the grid",
           "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto anchor = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 66, 1.0f, &undo);
    auto other = ProjectEdits::addNote (h.pattern(), 1, 3, 1, 69, 1.0f, &undo);

    h.roll.setSnap (SnapDivision::quarter);

    // Select both, then drag the lower one to around step 9.
    clickAndRelease (h.roll, pointFor (h, 2, 66));
    clickAndRelease (h.roll, pointFor (h, 3, 69),
                     juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
    REQUIRE (h.roll.getNumSelectedNotes() == 2);

    dragBetween (h.roll, pointFor (h, 2, 66), pointFor (h, 9, 66));

    const auto anchorStep = (int) anchor[ids::step];

    // The note under the pointer is on a grid line...
    CHECK (anchorStep % 4 == 0);

    // ...and the chord kept the one-step offset it started with.
    CHECK ((int) other[ids::step] - anchorStep == 1);
    CHECK ((int) other[ids::pitch] == 69);
}

TEST_CASE ("shift suspends the grid for the length of a drag", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);

    h.roll.setSnap (SnapDivision::bar); // sixteen steps per cell

    // Press without shift, so the press selects rather than toggling selection,
    // then drag with it held.
    h.roll.mouseDown (eventAt (h.roll, pointFor (h, 0, 66)));

    const juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    h.roll.mouseDrag (eventAt (h.roll, pointFor (h, 5, 66), shift));
    h.roll.mouseUp (eventAt (h.roll, pointFor (h, 5, 66), shift));

    // A bar grid would have pulled this to 0 or 16.
    CHECK ((int) note[ids::step] == 5);
}

// --- paint tool --------------------------------------------------------------

TEST_CASE ("the paint tool writes a note in every cell a stroke crosses", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.setTool (RollTool::paint);

    dragBetween (h.roll, pointFor (h, 0, 66), pointFor (h, 5, 66), 24);

    // Six cells, 0 through 5.
    CHECK (h.countNotes() == 6);

    const auto notes = sortedNotes (h);
    REQUIRE (notes.size() == 6);

    for (int i = 0; i < notes.size(); ++i)
    {
        CHECK ((int) notes[i][ids::step] == i);
        CHECK ((int) notes[i][ids::pitch] == 66);
    }
}

TEST_CASE ("painting back over a cell does not stack a second note in it", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.setTool (RollTool::paint);

    const auto from = pointFor (h, 0, 66);
    const auto to = pointFor (h, 4, 66);

    h.roll.mouseDown (eventAt (h.roll, from));

    // Out and back again, twice, the way a hand actually moves.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i <= 16; ++i)
        {
            const auto t = (float) i / 16.0f;
            h.roll.mouseDrag (
                eventAt (h.roll, { juce::roundToInt ((float) from.x + t * (float) (to.x - from.x)),
                                   from.y }));
        }

        for (int i = 16; i >= 0; --i)
        {
            const auto t = (float) i / 16.0f;
            h.roll.mouseDrag (
                eventAt (h.roll, { juce::roundToInt ((float) from.x + t * (float) (to.x - from.x)),
                                   from.y }));
        }
    }

    h.roll.mouseUp (eventAt (h.roll, from));

    CHECK (h.countNotes() == 5);
}

TEST_CASE ("a whole paint stroke is one undo step", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.setTool (RollTool::paint);
    dragBetween (h.roll, pointFor (h, 0, 66), pointFor (h, 5, 66), 24);

    REQUIRE (h.countNotes() == 6);
    REQUIRE (h.document.getUndoManager().undo());
    CHECK (h.countNotes() == 0);
}

TEST_CASE ("the paint tool still moves a note you press on", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 66, 1.0f, &undo);

    h.roll.setTool (RollTool::paint);

    dragBetween (h.roll, pointFor (h, 2, 66), pointFor (h, 6, 66));

    // Moved, not painted over: one note, at its new step.
    CHECK (h.countNotes() == 1);
    CHECK ((int) note[ids::step] == 6);
}

// --- slice tool --------------------------------------------------------------

TEST_CASE ("the slice tool cuts every note the line crosses, once", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();

    for (const auto pitch : { 62, 66, 70 })
        ProjectEdits::addNote (h.pattern(), 1, 0, 8, pitch, 1.0f, &undo);

    REQUIRE (h.countNotes() == 3);

    h.roll.setTool (RollTool::slice);

    // A vertical sweep at step 4, from above the top note to below the bottom.
    const auto top = pointFor (h, 4, 72);
    const auto bottom = pointFor (h, 4, 60);
    dragBetween (h.roll, top, bottom);

    CHECK (h.countNotes() == 6);

    for (const auto& note : NoteTools::notesOnChannel (h.pattern(), 1))
    {
        const auto step = (int) note[ids::step];
        CHECK ((step == 0 || step == 4));
        CHECK ((int) note[ids::lengthSteps] == 4);
    }
}

TEST_CASE ("a slice is one undo step and leaves the fragments selected", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 0, 8, 66, 1.0f, &undo);

    h.roll.setTool (RollTool::slice);
    dragBetween (h.roll, pointFor (h, 4, 68), pointFor (h, 4, 64));

    REQUIRE (h.countNotes() == 2);
    CHECK (h.roll.getNumSelectedNotes() == 2);

    REQUIRE (h.document.getUndoManager().undo());
    CHECK (h.countNotes() == 1);
    CHECK ((int) note[ids::lengthSteps] == 8);
}

TEST_CASE ("a slice that crosses nothing changes nothing", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 0, 8, 66, 1.0f, &undo);

    h.roll.setTool (RollTool::slice);

    // Well above the note, and horizontal - which crosses no row's centre.
    dragBetween (h.roll, pointFor (h, 0, 76), pointFor (h, 8, 76));

    CHECK (h.countNotes() == 1);
}

// --- scope, transpose, quantize ----------------------------------------------

TEST_CASE ("an edit acts on the selection when there is one", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto selected = ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);
    auto untouched = ProjectEdits::addNote (h.pattern(), 1, 4, 1, 69, 1.0f, &undo);

    clickAndRelease (h.roll, pointFor (h, 0, 66));
    REQUIRE (h.roll.getNumSelectedNotes() == 1);

    h.roll.transposeScope (12);

    CHECK ((int) selected[ids::pitch] == 78);
    CHECK ((int) untouched[ids::pitch] == 69);
}

TEST_CASE ("with nothing selected an edit acts on the whole channel", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto a = ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);
    auto b = ProjectEdits::addNote (h.pattern(), 1, 4, 1, 69, 1.0f, &undo);
    auto otherChannel = ProjectEdits::addNote (h.pattern(), 2, 0, 1, 50, 1.0f, &undo);

    REQUIRE (h.roll.getNumSelectedNotes() == 0);

    h.roll.transposeScope (-1);

    CHECK ((int) a[ids::pitch] == 65);
    CHECK ((int) b[ids::pitch] == 68);

    // Another channel's notes are context, not material.
    CHECK ((int) otherChannel[ids::pitch] == 50);
}

TEST_CASE ("alt-arrow transposes by a semitone and an octave", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);

    const auto press = [&h] (int keyCode, juce::ModifierKeys mods = juce::ModifierKeys())
    { return h.roll.keyPressed (juce::KeyPress (keyCode, mods, 0)); };

    // ALT-arrow. The bare arrows moved to the keyboard cursor, which is the one
    // gesture a canvas that paints its contents most needs and the only way a
    // keyboard reaches a note at all; alt is the modifier dew already spends on
    // a view's other axis.
    const auto alt = juce::ModifierKeys (juce::ModifierKeys::altModifier);
    const auto altShift = juce::ModifierKeys (juce::ModifierKeys::altModifier
                                              | juce::ModifierKeys::shiftModifier);

    REQUIRE (press (juce::KeyPress::upKey, alt));
    CHECK ((int) note[ids::pitch] == 67);

    REQUIRE (press (juce::KeyPress::downKey, alt));
    CHECK ((int) note[ids::pitch] == 66);

    REQUIRE (press (juce::KeyPress::upKey, altShift));
    CHECK ((int) note[ids::pitch] == 78);

    REQUIRE (press (juce::KeyPress::downKey, altShift));
    CHECK ((int) note[ids::pitch] == 66);

    // And a bare arrow no longer moves the music: it moves the cursor, and
    // leaves the note where it is.
    REQUIRE (press (juce::KeyPress::upKey));
    CHECK ((int) note[ids::pitch] == 66);
}

TEST_CASE ("quantize pulls the channel onto the current grid", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto a = ProjectEdits::addNote (h.pattern(), 1, 1, 1, 66, 1.0f, &undo);
    auto b = ProjectEdits::addNote (h.pattern(), 1, 7, 1, 69, 1.0f, &undo);

    h.roll.setSnap (SnapDivision::quarter);
    h.roll.quantizeScope();

    CHECK ((int) a[ids::step] == 0);
    CHECK ((int) b[ids::step] == 8);

    REQUIRE (h.document.getUndoManager().undo());
    CHECK ((int) a[ids::step] == 1);
    CHECK ((int) b[ids::step] == 7);
}

TEST_CASE ("changing tool or grid does not disturb the selection", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);

    clickAndRelease (h.roll, pointFor (h, 0, 66));
    REQUIRE (h.roll.getNumSelectedNotes() == 1);

    // The selection is exactly what the next quantize or transpose is aimed at,
    // so reaching for the grid must not throw it away.
    h.roll.setSnap (SnapDivision::bar);
    CHECK (h.roll.getNumSelectedNotes() == 1);

    h.roll.setTool (RollTool::slice);
    CHECK (h.roll.getNumSelectedNotes() == 1);
}

// --- the randomize dialog ----------------------------------------------------

TEST_CASE ("the randomize panel reports what its controls show", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RandomizePanel panel { { 0.4, 3 }, "Applies to 5 notes" };

    CHECK (juce::exactlyEqual (panel.getOptions().velocityAmount, 0.4));
    CHECK (panel.getOptions().stepAmount == 3);

    panel.setOptions ({ 0.1, 0 });
    CHECK (juce::exactlyEqual (panel.getOptions().velocityAmount, 0.1));
    CHECK (panel.getOptions().stepAmount == 0);
}

TEST_CASE ("cancelling the randomize panel applies nothing", "[ui][rolltools]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RandomizePanel panel { { 0.4, 3 }, "Applies to 5 notes" };

    bool applied = false;
    panel.onApply = [&applied] (const NoteTools::RandomizeOptions&) { applied = true; };

    // Invoked directly rather than through triggerClick, which posts a message
    // that nothing pumps in a headless test - this is the same callback the
    // button holds, just called without a message loop in the way.
    REQUIRE (panel.getCancelButton().onClick != nullptr);
    panel.getCancelButton().onClick();
    CHECK (! applied);

    REQUIRE (panel.getApplyButton().onClick != nullptr);
    panel.getApplyButton().onClick();
    CHECK (applied);
}

// --- persistence -------------------------------------------------------------

TEST_CASE ("the snap grid survives a session, and a corrupt one falls back", "[ui][rolltools]")
{
    juce::TemporaryFile temp;
    const auto directory = temp.getFile().getParentDirectory().getChildFile (
        "dew-snap-" + juce::String (juce::Random().nextInt (99999)));
    directory.createDirectory();

    {
        Settings settings { directory };
        settings.setPianoRollSnap (NoteTools::indexOfSnap (SnapDivision::half));
        settings.flush();
    }

    {
        Settings settings { directory };
        CHECK (NoteTools::snapFromIndex (settings.getPianoRollSnap()) == SnapDivision::half);

        settings.setPianoRollSnap (99);
        CHECK (settings.getPianoRollSnap() < NoteTools::numSnapDivisions);
    }

    directory.deleteRecursively();
}
