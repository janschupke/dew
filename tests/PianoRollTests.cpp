// Writing, selecting and moving notes, and the grid they sit on.
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

TEST_CASE ("pinching zooms around the pointer", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto area = h.roll.getNoteArea();
    const auto anchor = juce::Point<int> (area.getCentreX(), area.getCentreY());

    const auto before = h.roll.getTimeline().pixelsPerStep;
    const auto stepUnderAnchor = h.roll.getTimeline().stepForX (
        (float) (anchor.x - h.roll.getKeyboardArea().getWidth()));

    h.roll.mouseMagnify (eventAt (h.roll, anchor), 2.0f);

    INFO ("pixelsPerStep " << before << " -> " << h.roll.getTimeline().pixelsPerStep);
    REQUIRE (h.roll.getTimeline().pixelsPerStep > before);

    // And the music did not walk out from under the fingers doing the pinching.
    const auto after = h.roll.getTimeline().stepForX (
        (float) (anchor.x - h.roll.getKeyboardArea().getWidth()));
    REQUIRE (std::abs (after - stepUnderAnchor) < 1.0e-6);
}

TEST_CASE ("the roll frames the pattern rather than opening at an arbitrary zoom",
           "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // A 16-step pattern fits in 1200px, so all of it should be visible.
    REQUIRE (h.roll.getTimeline().visibleSteps (1200.0f - 54.0f - 10.0f) >= 16.0);

    juce::UndoManager& undo = h.document.getUndoManager();
    h.pattern().setProperty (ids::lengthSteps, 256, &undo);
    h.roll.zoomToFit();

    // 256 steps do not fit at a legible zoom, so the view scrolls instead of
    // squashing them into nothing.
    const auto& timeline = h.roll.getTimeline();
    REQUIRE (timeline.pixelsPerStep >= TimelineView::minPixelsPerStep);
    REQUIRE (juce::exactlyEqual (timeline.scrollOffsetSteps, 0.0));
}

TEST_CASE ("the roll scrolls to the notes rather than opening on empty keys", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;

    document.setState (dew::testing::fixtureProject(), true);

    PianoRollComponent roll { document, engine, editorState };
    roll.setSize (1200, 600);
    roll.setVisible (true);
    roll.refresh();
    roll.resized();

    const auto notesVisible = [&roll, &document, &editorState]
    {
        const auto pattern = ProjectEdits::findPattern (document.getState(),
                                                        editorState.getCurrentPatternId());
        int visible = 0;

        for (const auto& note : pattern)
            if (note.hasType (ids::NOTE)
                && (int) note[ids::ch] == editorState.getSelectedChannelId()
                && roll.getNoteArea().toFloat().intersects (roll.getBoundsForNote (note)))
                ++visible;

        return visible;
    };

    // The demo's first channel is a kick around C2, well below the middle of the
    // keyboard - opening there used to show a blank grid.
    REQUIRE (notesVisible() > 0);

    // Every channel in the demo, whatever register it is written in.
    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        editorState.setSelectedChannelId ((int) channel[ids::id]);

        // EditorState is a ChangeBroadcaster, and its message is asynchronous:
        // in the app the roll re-frames from changeListenerCallback once the
        // message loop runs, but nothing pumps it here. Driving the re-frame
        // directly is what makes this a test of the framing rather than of
        // whichever pitches the opening view happened to span - which is what
        // it was, and it passed only while the note area stayed tall enough to
        // show every demo channel at once.
        roll.scrollToNotesIfOffscreen();

        INFO ("channel " << channel[ids::name].toString());
        REQUIRE (notesVisible() > 0);
    }
}

TEST_CASE ("a view that already shows the notes is left alone", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addNote (h.pattern(), 1, 0, 1, 72, 1.0f, &undo);

    const auto before = h.roll.getBoundsForNote (h.pattern().getChild (0)).getY();

    h.roll.scrollToNotesIfOffscreen();

    // The note was already on screen, so nothing moved under the user's hands.
    REQUIRE (
        juce::exactlyEqual (h.roll.getBoundsForNote (h.pattern().getChild (0)).getY(), before));
}

TEST_CASE ("notes never paint over the keyboard", "[ui][pianoroll]")
{
    // paintNotes clipped from x = 0, which included the keyboard gutter, so a
    // note scrolled past the left edge was drawn straight across the keys.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();

    // A long, loud note at the very start, on a channel with a known colour.
    auto channel = h.document.getState().getChildWithName (ids::CHANNEL);
    channel.setProperty (ids::colour, "ffe4572e", &undo);
    ProjectEdits::addNote (h.pattern(), 1, 0, 16, 66, 1.0f, &undo);

    // Zoom in hard, anchored at the right of the view, so the pattern is much
    // wider than the note area and the start of it scrolls off the left edge.
    // Fitting first would clamp the scroll to zero and prove nothing.
    for (int i = 0; i < 6; ++i)
        h.roll.mouseMagnify (eventAt (h.roll, { h.roll.getNoteArea().getRight() - 4,
                                                h.roll.getNoteArea().getCentreY() }),
                             2.0f);

    INFO ("scroll offset " << h.roll.getTimeline().scrollOffsetSteps);
    REQUIRE (h.roll.getTimeline().scrollOffsetSteps > 1.0);

    juce::Image image (juce::Image::ARGB, h.roll.getWidth(), h.roll.getHeight(), true);
    juce::Graphics g (image);
    h.roll.paintEntireComponent (g, true);

    const auto keys = h.roll.getKeyboardArea();
    // The first ramp entry, read from where it is declared: hard-coding the
    // hex made this test fail for a reason that had nothing to do with the
    // piano roll the day the palette changed.
    const auto rampColour = juce::Colour::fromString (dew::entityColour::defaultHex (0));

    int bleeding = 0;

    for (int y = keys.getY(); y < keys.getBottom(); y += 2)
        for (int x = keys.getX(); x < keys.getRight() - 1; x += 2)
        {
            const auto pixel = image.getPixelAt (x, y);

            if (std::abs ((int) pixel.getRed() - (int) rampColour.getRed()) < 24
                && std::abs ((int) pixel.getGreen() - (int) rampColour.getGreen()) < 24
                && std::abs ((int) pixel.getBlue() - (int) rampColour.getBlue()) < 24)
                ++bleeding;
        }

    INFO ("channel-coloured pixels inside the keyboard gutter: " << bleeding);
    REQUIRE (bleeding == 0);
}

TEST_CASE ("the grid keeps drawing past the end of a short pattern", "[pianoroll][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h { 1200, 700 };

    // Four steps in a 1200px window: zoomToFit caps at 120px per step, so the
    // pattern occupies 480px and roughly half the note area is past its end.
    // That is exactly the case that used to be a hatched dead rectangle.
    h.pattern().setProperty (ids::lengthSteps, 4, nullptr);
    h.roll.refresh();
    h.roll.zoomToFit();

    const auto notes = h.roll.getNoteArea();
    const auto endX = (float) notes.getX() + (float) h.roll.getTimeline().xForStep (4.0);

    REQUIRE (endX < (float) notes.getRight() - 100.0f);

    juce::Image image (juce::Image::ARGB, h.roll.getWidth(), h.roll.getHeight(), true);
    juce::Graphics g (image);
    h.roll.paintEntireComponent (g, true);

    // Mean brightness down a whole column. A vertical grid line is bright at
    // every y, so it shows as a tall peak; the diagonal hatch this replaced
    // crosses each column at ONE y and cannot produce one.
    const auto columnMean = [&image, notes] (int x)
    {
        double total = 0.0;

        for (int y = notes.getY() + 4; y < notes.getBottom() - 4; ++y)
            total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, notes.getHeight() - 8);
    };

    juce::Array<double> beyond;

    for (int x = (int) endX + 4; x < notes.getRight() - 2; ++x)
        beyond.add (columnMean (x));

    REQUIRE (beyond.size() > 100);

    auto sorted = beyond;
    sorted.sort();
    const auto background = sorted[sorted.size() / 2];

    int verticalLines = 0;

    for (auto value : beyond)
        if (value > background * 1.15 + 0.002)
            ++verticalLines;

    INFO ("vertical grid lines past the pattern end: "
          << verticalLines << " (background column mean " << background << ")");
    REQUIRE (verticalLines >= 3);
}

TEST_CASE ("the end of the pattern is marked, and past it is dimmer", "[pianoroll][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h { 1200, 700 };

    h.pattern().setProperty (ids::lengthSteps, 4, nullptr);
    h.roll.refresh();
    h.roll.zoomToFit();

    const auto notes = h.roll.getNoteArea();
    const auto endX = notes.getX() + (int) h.roll.getTimeline().xForStep (4.0);

    juce::Image image (juce::Image::ARGB, h.roll.getWidth(), h.roll.getHeight(), true);
    juce::Graphics g (image);
    h.roll.paintEntireComponent (g, true);

    const auto meanOver = [&image] (juce::Rectangle<int> area)
    {
        double total = 0.0;
        int counted = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x, ++counted)
                total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, counted);
    };

    // A rule at the pattern end, at full strength - it is what carries the
    // meaning now that the region past it is no longer blanked out.
    const auto ruleMean = meanOver ({ endX - 1, notes.getY() + 4, 3, notes.getHeight() - 8 });
    const auto nearbyMean = meanOver ({ endX + 20, notes.getY() + 4, 3, notes.getHeight() - 8 });

    INFO ("rule " << ruleMean << " vs nearby " << nearbyMean);
    REQUIRE (ruleMean > nearbyMean * 2.0);

    // And past it reads as out of bounds without being blank.
    const auto insideMean = meanOver ({ endX - 120, notes.getY() + 4, 100, notes.getHeight() - 8 });
    const auto beyondMean = meanOver ({ endX + 20, notes.getY() + 4, 100, notes.getHeight() - 8 });

    INFO ("inside " << insideMean << " vs beyond " << beyondMean);
    REQUIRE (beyondMean < insideMean);
    REQUIRE (beyondMean > 0.0);
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
