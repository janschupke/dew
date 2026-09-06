// The velocity lane, and the keyboard that auditions.
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

TEST_CASE ("a velocity bar is grabbed and dragged", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 4, 1, 72, 1.0f, &undo);

    REQUIRE ((double) note[ids::velocity] > 0.9);

    const auto lane = h.roll.getVelocityArea();
    const auto bar = h.roll.getVelocityBarBounds (note);

    // The bar is a few pixels wide and its top is what you aim at. At full
    // velocity that top sits near the lane's ceiling.
    INFO ("lane " << lane.toString() << " bar " << bar.toString());
    REQUIRE (bar.getY() < (float) lane.getY() + 8.0f);
    REQUIRE (bar.getBottom() > (float) lane.getBottom() - 8.0f);

    const auto x = (int) bar.getCentreX();

    // Grab the top of the bar and drag it to the floor of the lane.
    h.roll.mouseDown (eventAt (h.roll, { x, (int) bar.getY() }));
    h.roll.mouseDrag (eventAt (h.roll, { x, lane.getBottom() - 4 }));
    h.roll.mouseUp (eventAt (h.roll, { x, lane.getBottom() - 4 }));

    const auto quiet = (double) note[ids::velocity];
    INFO ("after dragging to the floor: " << quiet);
    REQUIRE (quiet < 0.15);
    REQUIRE (quiet >= 0.05); // never silent, which would read as a bug

    // And back to the top.
    const auto low = h.roll.getVelocityBarBounds (note);
    h.roll.mouseDown (eventAt (h.roll, { x, (int) low.getY() }));
    h.roll.mouseDrag (eventAt (h.roll, { x, lane.getY() + 2 }));
    h.roll.mouseUp (eventAt (h.roll, { x, lane.getY() + 2 }));

    REQUIRE ((double) note[ids::velocity] > 0.9);
}

TEST_CASE ("the bar you grab is the one that moves", "[ui][pianoroll]")
{
    // The lane's geometry and its hit-test used to disagree by 8px, and any
    // click snapped a value rather than grabbing anything.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    auto first = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 72, 0.9f, &undo);
    auto second = ProjectEdits::addNote (h.pattern(), 1, 9, 1, 72, 0.9f, &undo);

    const auto lane = h.roll.getVelocityArea();
    const auto bar = h.roll.getVelocityBarBounds (first);
    const auto x = (int) bar.getCentreX();

    h.roll.mouseDown (eventAt (h.roll, { x, (int) bar.getY() }));
    h.roll.mouseDrag (eventAt (h.roll, { x, lane.getCentreY() }));
    h.roll.mouseUp (eventAt (h.roll, { x, lane.getCentreY() }));

    REQUIRE ((double) first[ids::velocity] < 0.8);
    REQUIRE ((double) second[ids::velocity] > 0.85);
}

TEST_CASE ("clicking empty lane space does nothing at all", "[ui][pianoroll]")
{
    // It used to open an undo transaction and edit nothing.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 72, 0.9f, &undo);

    const auto lane = h.roll.getVelocityArea();

    undo.beginNewTransaction ("Before");
    const auto before = undo.getNumberOfUnitsTakenUpByStoredCommands();

    // Far to the right of any bar.
    h.roll.mouseDown (eventAt (h.roll, { lane.getRight() - 20, lane.getCentreY() }));
    h.roll.mouseUp (eventAt (h.roll, { lane.getRight() - 20, lane.getCentreY() }));

    REQUIRE (undo.getNumberOfUnitsTakenUpByStoredCommands() == before);
}

TEST_CASE ("a velocity drag that misses a bar still paints the ones it crosses", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    auto first = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 72, 0.9f, &undo);
    auto second = ProjectEdits::addNote (h.pattern(), 1, 9, 1, 72, 0.9f, &undo);

    const auto lane = h.roll.getVelocityArea();
    const auto firstBar = h.roll.getVelocityBarBounds (first);
    const auto secondBar = h.roll.getVelocityBarBounds (second);

    // A bar is three pixels wide at any zoom worth using. Requiring the press
    // to land on one made the whole lane inert, because a press that missed
    // did not merely do nothing - it abandoned the gesture, so the drag that
    // followed did nothing either.
    const auto startX = (int) firstBar.getX() - 30;
    const auto y = lane.getBottom() - 6;

    REQUIRE (startX > lane.getX());

    h.roll.mouseDown (eventAt (h.roll, { startX, y }));
    h.roll.mouseDrag (eventAt (h.roll, { (int) firstBar.getCentreX(), y }, {}, 1, true));
    h.roll.mouseDrag (eventAt (h.roll, { (int) secondBar.getCentreX(), y }, {}, 1, true));
    h.roll.mouseUp (eventAt (h.roll, { (int) secondBar.getCentreX(), y }, {}, 1, true));

    INFO ("first " << (double) first[ids::velocity] << " second "
                   << (double) second[ids::velocity]);
    CHECK ((double) first[ids::velocity] < 0.3);
    CHECK ((double) second[ids::velocity] < 0.3);
}

TEST_CASE ("clicking a piano key auditions it", "[ui][pianoroll]")
{
    // The keyboard gutter was never tested in mouseDown, so clicking a key did
    // nothing - which is exactly what "piano keys unclickable" described.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto keys = h.roll.getKeyboardArea();
    REQUIRE (keys.getWidth() > 0);

    REQUIRE (h.roll.getAuditionPitch() == -1);

    const juce::Point<int> onAKey { keys.getCentreX(), keys.getY() + 40 };
    h.roll.mouseDown (eventAt (h.roll, onAKey));

    const auto pitch = h.roll.getAuditionPitch();
    INFO ("auditioned pitch " << pitch);
    REQUIRE (pitch >= 0);

    // Sliding down the keyboard plays what it passes over.
    h.roll.mouseDrag (eventAt (h.roll, { onAKey.x, onAKey.y + 60 }));
    REQUIRE (h.roll.getAuditionPitch() != pitch);
    REQUIRE (h.roll.getAuditionPitch() < pitch); // further down is lower

    // Letting go releases it.
    h.roll.mouseUp (eventAt (h.roll, { onAKey.x, onAKey.y + 60 }));
    REQUIRE (h.roll.getAuditionPitch() == -1);
}

TEST_CASE ("auditioning a key writes no notes", "[ui][pianoroll]")
{
    // The gutter sits beside the note area; a key press must not also draw.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE (h.countNotes() == 0);

    const auto keys = h.roll.getKeyboardArea();
    h.roll.mouseDown (eventAt (h.roll, { keys.getCentreX(), keys.getCentreY() }));
    h.roll.mouseDrag (eventAt (h.roll, { keys.getCentreX(), keys.getCentreY() + 30 }));
    h.roll.mouseUp (eventAt (h.roll, { keys.getCentreX(), keys.getCentreY() + 30 }));

    REQUIRE (h.countNotes() == 0);
}

TEST_CASE ("the velocity lane can be made taller by its own edge", "[ui][pianoroll]")
{
    // The lane's height was a static constexpr 62 while its cursor was an
    // up-down arrow over every pixel of it. The pointer promised a gesture the
    // view had no state to perform, and a press aimed at the boundary wrote a
    // velocity of nearly one into whatever note was under it instead.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE (h.roll.getVelocityHeight() == tokens::size::velocityLaneDefault);

    const auto grab = h.roll.getVelocityResizeArea().getCentre();

    // Up is taller: the lane's top edge moves towards the ruler.
    h.roll.mouseDown (eventAt (h.roll, grab));
    h.roll.mouseDrag (eventAt (h.roll, { grab.x, grab.y - 40 }, {}, 1, true));
    h.roll.mouseUp (eventAt (h.roll, { grab.x, grab.y - 40 }, {}, 1, true));

    CHECK (h.roll.getVelocityHeight() == tokens::size::velocityLaneDefault + 40);

    // And the note grid gave up exactly what the lane took, so the two still
    // meet - which is the invariant a second layout constant would break.
    CHECK (h.roll.getNoteArea().getBottom() == h.roll.getVelocityArea().getY());
}

TEST_CASE ("the velocity lane clamps at both ends of its range", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto dragEdgeBy = [&h] (int deltaY)
    {
        const auto grab = h.roll.getVelocityResizeArea().getCentre();
        h.roll.mouseDown (eventAt (h.roll, grab));
        h.roll.mouseDrag (eventAt (h.roll, { grab.x, grab.y - deltaY }, {}, 1, true));
        h.roll.mouseUp (eventAt (h.roll, { grab.x, grab.y - deltaY }, {}, 1, true));
    };

    dragEdgeBy (-1000);
    CHECK (h.roll.getVelocityHeight() == tokens::size::velocityLaneMin);

    dragEdgeBy (1000);

    // The ceiling is the range's, or whatever the window can spare while still
    // leaving a row to write notes in - whichever is smaller.
    CHECK (h.roll.getVelocityHeight() <= tokens::size::velocityLaneMax);
    CHECK (h.roll.getVelocityHeight() > tokens::size::velocityLaneDefault);
    CHECK (h.roll.getNoteArea().getHeight() >= tokens::size::pianoRowMin);
}

TEST_CASE ("a right press on the lane's edge starts nothing", "[ui][pianoroll]")
{
    // The rule every other control in dew follows, applied to the one gesture
    // in this view that is a resize. A popup press must not resize and must not
    // write a velocity either.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    auto note = ProjectEdits::addNote (h.pattern(), 1, 0, 4, 72, 0.5f, &undo);
    undo.beginNewTransaction();

    const auto grab = h.roll.getVelocityResizeArea().getCentre();
    const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);

    h.roll.mouseDown (eventAt (h.roll, grab, right));
    h.roll.mouseDrag (eventAt (h.roll, { grab.x, grab.y - 40 }, right, 1, true));
    h.roll.mouseUp (eventAt (h.roll, { grab.x, grab.y - 40 }, right, 1, true));

    CHECK (h.roll.getVelocityHeight() == tokens::size::velocityLaneDefault);
    CHECK (juce::exactlyEqual ((double) note[ids::velocity], 0.5));
}

TEST_CASE ("a double-click on the lane's edge puts it back", "[ui][pianoroll]")
{
    // The same gesture a playlist track header offers for its lane height.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.setVelocityHeight (tokens::size::velocityLaneDefault + 60);
    REQUIRE (h.roll.getVelocityHeight() == tokens::size::velocityLaneDefault + 60);

    h.roll.mouseDoubleClick (eventAt (h.roll, h.roll.getVelocityResizeArea().getCentre(), {}, 2));

    CHECK (h.roll.getVelocityHeight() == tokens::size::velocityLaneDefault);
}

TEST_CASE ("where a key is pressed across is how hard it sounds", "[ui][pianoroll]")
{
    // The keyboard's 54 pixels were doing nothing at all: mouseDown had the x
    // of the press one line above and threw it away, so every key sounded at
    // whatever velocity the roll had last DRAWN a note with - a number that has
    // nothing to do with the press and that a person auditioning a sound has no
    // way to change without drawing something first.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto near = PianoRollComponent::auditionVelocityForX (0);
    const auto middle = PianoRollComponent::auditionVelocityForX (tokens::size::gutterKeyboard / 2);
    const auto far = PianoRollComponent::auditionVelocityForX (tokens::size::gutterKeyboard - 1);

    INFO ("near " << near << ", middle " << middle << ", far " << far);
    CHECK (near < middle);
    CHECK (middle < far);

    // The near edge still SOUNDS. A keyboard with a silent strip down one side
    // reads as a keyboard with a dead spot, so this is floored at the same 0.05
    // EditorState::rememberNote clamps a drawn note to.
    CHECK (near >= 0.05f);
    CHECK (far <= 1.0f);

    // Past the gutter cannot exceed full - a drag does not stop at the edge.
    CHECK (PianoRollComponent::auditionVelocityForX (tokens::size::gutterKeyboard * 4) <= 1.0f);
}
