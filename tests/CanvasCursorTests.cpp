#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/CanvasCursor.h"
#include "PaintProbe.h"
#include "PlaylistHarness.h"
#include "RollHarness.h"
#include "StepGridHarness.h"

using namespace dew;
using namespace dew::testing;

/*  The keyboard's own position on a canvas that paints its contents.

    The piano roll, the playlist and the step grid draw their notes, clips and
    cells rather than parenting them, so until this there was nothing for a
    keyboard to land on and nothing for a screen reader to meet: three of dew's
    five tabs could only be edited with a mouse.

    ControlWalkHarness cannot see any of this - it finds controls by TYPE, and a
    cursor is not a component - so these are their own tests.
*/

namespace
{

juce::KeyPress arrow (int keyCode)
{
    return { keyCode, juce::ModifierKeys(), 0 };
}

} // namespace

TEST_CASE ("a cursor stays inside its limits and reports whether it moved", "[ui][cursor]")
{
    // The type on its own, before any view. Its whole job is to be somewhere
    // and to refuse to be nowhere.
    CanvasCursor cursor;
    const juce::Rectangle<int> limits { 0, 0, 4, 3 };

    CHECK_FALSE (cursor.isPlaced());

    // The FIRST move places it at the origin rather than stepping from a
    // position it never had - so the first left-arrow on an untouched canvas
    // does not land at -1 and clamp to 0 having "moved".
    REQUIRE (cursor.moveBy ({ -1, 0 }, limits));
    CHECK (cursor.getPosition() == juce::Point<int> (0, 0));

    REQUIRE (cursor.moveBy ({ 1, 1 }, limits));
    CHECK (cursor.getPosition() == juce::Point<int> (1, 1));

    // At the edge it stays put and says so, which is what lets a view repaint
    // and announce only when something happened.
    CHECK (cursor.moveTo ({ 99, 99 }, limits));
    CHECK (cursor.getPosition() == juce::Point<int> (3, 2));
    CHECK_FALSE (cursor.moveBy ({ 1, 1 }, limits));

    // An empty canvas has nowhere to be.
    CanvasCursor fresh;
    CHECK_FALSE (fresh.moveBy ({ 1, 0 }, { 0, 0, 0, 0 }));
    CHECK_FALSE (fresh.isPlaced());
}

TEST_CASE ("the step grid's arrows move a cursor and Return toggles under it", "[ui][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    GridHarness h;

    const auto lit = [&h]
    {
        auto count = 0;

        for (const auto& note : h.pattern())
            if (note.hasType (ids::NOTE))
                ++count;

        return count;
    };

    const auto before = lit();

    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::rightKey)));
    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::rightKey)));

    // Return acts on where the keyboard is. Twice is a toggle: the second press
    // must undo the first, or "activate" means "add" and the keyboard can only
    // ever fill a pattern up.
    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::returnKey)));
    const auto added = lit();
    CHECK (added == before + 1);

    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::returnKey)));
    CHECK (lit() == before);
}

TEST_CASE ("a cursor is drawn where it is, and not before it is placed", "[ui][cursor]")
{
    // WCAG 2.4.7 for a surface with no components in it. Measured rather than
    // asserted: paint::cursorOutline takes the flag for the same reason
    // paint::focusRing does - grabKeyboardFocus does nothing without a peer, so
    // a painter that asked the component could never be shown to draw.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    GridHarness h;

    const auto blank = coverageOf (h.render(), tokens::colour::accent);

    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::rightKey)));

    const auto placed = coverageOf (h.render(), tokens::colour::accent);

    INFO ("accent coverage " << blank << " -> " << placed);
    CHECK (placed > blank);
}

TEST_CASE ("the roll's arrows move a cursor, and alt-arrow still transposes", "[ui][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 0, 1, 66, 1.0f, &undo);

    const auto pitchBefore = (int) note[ids::pitch];

    // A bare arrow moves the keyboard and leaves the music alone. This is the
    // binding that changed, and it is the whole reason the roll is reachable.
    REQUIRE (h.roll.keyPressed (arrow (juce::KeyPress::upKey)));
    CHECK ((int) note[ids::pitch] == pitchBefore);

    // Alt still moves the music.
    REQUIRE (h.roll.keyPressed (
        { juce::KeyPress::upKey, juce::ModifierKeys (juce::ModifierKeys::altModifier), 0 }));
    CHECK ((int) note[ids::pitch] == pitchBefore + 1);
}

TEST_CASE ("the roll's Return toggles a note where the cursor is", "[ui][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto before = h.countNotes();

    REQUIRE (h.roll.keyPressed (arrow (juce::KeyPress::rightKey)));
    REQUIRE (h.roll.keyPressed (arrow (juce::KeyPress::returnKey)));
    CHECK (h.countNotes() == before + 1);

    // And removes the note it can hear, not merely one that starts here.
    REQUIRE (h.roll.keyPressed (arrow (juce::KeyPress::returnKey)));
    CHECK (h.countNotes() == before);
}

TEST_CASE ("the playlist's arrows move a cursor without touching the document", "[ui][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PlaylistHarness h;

    const auto clipsBefore = h.countClips (0);

    for (int i = 0; i < 3; ++i)
    {
        REQUIRE (h.playlist.keyPressed (arrow (juce::KeyPress::rightKey)));
        REQUIRE (h.playlist.keyPressed (arrow (juce::KeyPress::downKey)));
    }

    // Moving is not editing. The playlist has no clip selection and no delete
    // key by design, so an arrow that changed the arrangement would be a
    // surprise nothing undid.
    CHECK (h.countClips (0) == clipsBefore);
}

TEST_CASE ("a cursor survives the edit made under it", "[ui][cursor]")
{
    // The reason a cursor is a COORDINATE and not a juce::ValueTree. Hold the
    // note and deleting it leaves the cursor pointing at a tree that is no
    // longer in the document; hold the place and the cursor is still somewhere.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    GridHarness h;

    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::rightKey)));
    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::returnKey)));

    h.document.getUndoManager().undo();

    // Still movable, still drawn, still where it was.
    REQUIRE (h.grid.keyPressed (arrow (juce::KeyPress::rightKey)));
    CHECK (coverageOf (h.render(), tokens::colour::accent) > 0.0f);
}
