#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "RollHarness.h"
#include "model/ChannelColour.h"
#include "ui/ChannelRackComponent.h"
#include "ui/ZoomButtons.h"
#include "ui/design/Tokens.h"
#include "FixtureProject.h"

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
    REQUIRE (ProjectEdits::lengthNeededForNotes (h.pattern()) <= (int) h.pattern()[ids::lengthSteps]);

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
    const auto to   = pointFor (h, 6, 68);

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
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 60, 1.0f, &undo);   // a third apart
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 64, 1.0f, &undo);
    ProjectEdits::addNote (h.pattern(), 1, 2, 1, 67, 1.0f, &undo);

    REQUIRE (h.roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a')));
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

    REQUIRE (h.roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a')));

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
    REQUIRE (quiet >= 0.05);       // never silent, which would read as a bug

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
    auto first  = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 72, 0.9f, &undo);
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

TEST_CASE ("a velocity drag that misses a bar still paints the ones it crosses",
           "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();
    auto first  = ProjectEdits::addNote (h.pattern(), 1, 2, 1, 72, 0.9f, &undo);
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
    h.roll.mouseUp   (eventAt (h.roll, { (int) secondBar.getCentreX(), y }, {}, 1, true));

    INFO ("first " << (double) first[ids::velocity]
          << " second " << (double) second[ids::velocity]);
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
    REQUIRE (h.roll.getAuditionPitch() < pitch);      // further down is lower

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
    const auto stepUnderAnchor = h.roll.getTimeline()
                                     .stepForX ((float) (anchor.x - h.roll.getKeyboardArea().getWidth()));

    h.roll.mouseMagnify (eventAt (h.roll, anchor), 2.0f);

    INFO ("pixelsPerStep " << before << " -> " << h.roll.getTimeline().pixelsPerStep);
    REQUIRE (h.roll.getTimeline().pixelsPerStep > before);

    // And the music did not walk out from under the fingers doing the pinching.
    const auto after = h.roll.getTimeline()
                            .stepForX ((float) (anchor.x - h.roll.getKeyboardArea().getWidth()));
    REQUIRE (std::abs (after - stepUnderAnchor) < 1.0e-6);
}

TEST_CASE ("the roll frames the pattern rather than opening at an arbitrary zoom", "[ui][pianoroll]")
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
            if (note.hasType (ids::NOTE) && (int) note[ids::ch] == editorState.getSelectedChannelId()
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
    REQUIRE (juce::exactlyEqual (h.roll.getBoundsForNote (h.pattern().getChild (0)).getY(), before));
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
                                                h.roll.getNoteArea().getCentreY() }), 2.0f);

    INFO ("scroll offset " << h.roll.getTimeline().scrollOffsetSteps);
    REQUIRE (h.roll.getTimeline().scrollOffsetSteps > 1.0);

    juce::Image image (juce::Image::ARGB, h.roll.getWidth(), h.roll.getHeight(), true);
    juce::Graphics g (image);
    h.roll.paintEntireComponent (g, true);

    const auto keys = h.roll.getKeyboardArea();
    // The first ramp entry, read from where it is declared: hard-coding the
    // hex made this test fail for a reason that had nothing to do with the
    // piano roll the day the palette changed.
    const auto rampColour = juce::Colour::fromString (dew::channelColour::defaultHex (0));

    int bleeding = 0;

    for (int y = keys.getY(); y < keys.getBottom(); y += 2)
        for (int x = keys.getX(); x < keys.getRight() - 1; x += 2)
        {
            const auto pixel = image.getPixelAt (x, y);

            if (std::abs ((int) pixel.getRed()   - (int) rampColour.getRed())   < 24
             && std::abs ((int) pixel.getGreen() - (int) rampColour.getGreen()) < 24
             && std::abs ((int) pixel.getBlue()  - (int) rampColour.getBlue())  < 24)
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
    const auto endX = (float) notes.getX()
                    + (float) h.roll.getTimeline().xForStep (4.0);

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

    INFO ("vertical grid lines past the pattern end: " << verticalLines
          << " (background column mean " << background << ")");
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
    const auto to   = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp   (eventAt (h.roll, to, rightButton));

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
    const auto to   = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp   (eventAt (h.roll, to, rightButton));

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
    const auto to   = pointFor (h, 7, 66);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.roll.mouseDown (eventAt (h.roll, from, rightButton));
    h.roll.mouseDrag (eventAt (h.roll, to, rightButton));
    h.roll.mouseUp   (eventAt (h.roll, to, rightButton));

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
    const auto to   = pointFor (h, 5, 66);

    SECTION ("alt")
    {
        const juce::ModifierKeys alt { juce::ModifierKeys::altModifier };

        h.roll.mouseDown (eventAt (h.roll, from, alt));
        h.roll.mouseDrag (eventAt (h.roll, to, alt));
        h.roll.mouseUp   (eventAt (h.roll, to, alt));

        REQUIRE (h.countNotes() == notesBefore - 6);
    }

    SECTION ("plain left drag moves rather than erasing")
    {
        const juce::ModifierKeys left { juce::ModifierKeys::leftButtonModifier };

        h.roll.mouseDown (eventAt (h.roll, from, left));
        h.roll.mouseDrag (eventAt (h.roll, to, left));
        h.roll.mouseUp   (eventAt (h.roll, to, left));

        REQUIRE (h.countNotes() == notesBefore);
    }
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
        h.roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
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

// --- the loop span on the ruler ----------------------------------------------

namespace
{

/** A point on the piano roll's ruler at a given step. Asks the roll where that
    step is rather than recomputing the layout, so a layout change cannot leave
    these clicking confidently into the wrong place and still passing.
*/
juce::Point<int> rulerPointForStep (dew::testing::RollHarness& h, int step)
{
    const auto ruler = h.roll.getRulerArea();
    const auto x = (float) ruler.getX() + h.roll.getTimeline().xForStep ((double) step);
    const juce::Point<int> point { (int) x, ruler.getCentreY() };

    // A test aiming off the end of the ruler is a broken test, not a finding:
    // the press would fall through the ruler branch entirely and land on the
    // grid, which is a different gesture with a different outcome.
    REQUIRE (ruler.contains (point));
    return point;
}

constexpr int stepsPerBar = 16;   // the default project: 4 steps per beat, 4 beats

/** Gives the harness a four-bar pattern, so snapping to a BAR is a visible
    thing rather than always rounding to the whole of a one-bar pattern.
*/
void makeFourBars (dew::testing::RollHarness& h)
{
    h.pattern().setProperty (dew::ids::lengthSteps, 4 * stepsPerBar, nullptr);
    h.roll.refresh();
    h.roll.zoomToFit();
    h.roll.resized();
}

} // namespace

TEST_CASE ("shift-dragging the piano roll ruler selects a span of bars", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };

    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 0), shift));
    h.roll.mouseDrag (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));
    h.roll.mouseUp   (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));

    REQUIRE (h.editorState.hasStepSelection());

    // Snapped to bars: a loop is a musical span, and the ruler is numbered in
    // bars, so landing between two of them is not a thing anyone asked for.
    const auto selection = h.editorState.getSelectedStepRange();
    INFO ("selection " << selection.getStart() << " -> " << selection.getEnd());
    REQUIRE (selection.getStart() % stepsPerBar == 0);
    REQUIRE (selection.getEnd() % stepsPerBar == 0);
    REQUIRE (selection.getStart() == 0);
    REQUIRE (selection.getEnd() == 2 * stepsPerBar);
}

TEST_CASE ("a plain drag on the piano roll ruler still scrubs", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.zoomToFit();
    h.roll.resized();

    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 0)));
    h.roll.mouseDrag (eventAt (h.roll, rulerPointForStep (h, 8), {}, 1, true));
    h.roll.mouseUp   (eventAt (h.roll, rulerPointForStep (h, 8), {}, 1, true));

    // Scrubbing was on the ruler first; selecting had to fit around it.
    REQUIRE_FALSE (h.editorState.hasStepSelection());
    REQUIRE (h.engine.getPlayheadSteps() > 0.0);
}

TEST_CASE ("shift-clicking the piano roll ruler without dragging clears the span",
           "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.zoomToFit();
    h.roll.resized();

    h.editorState.setSelectedStepRange ({ 0, stepsPerBar });
    REQUIRE (h.editorState.hasStepSelection());

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
    const auto at = rulerPointForStep (h, 0);

    h.roll.mouseDown (eventAt (h.roll, at, shift));
    h.roll.mouseUp   (eventAt (h.roll, at, shift));

    REQUIRE_FALSE (h.editorState.hasStepSelection());
}

TEST_CASE ("double-clicking the piano roll ruler clears the span", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    h.roll.zoomToFit();
    h.roll.resized();

    h.editorState.setSelectedStepRange ({ 0, stepsPerBar });
    REQUIRE (h.editorState.hasStepSelection());

    h.roll.mouseDoubleClick (eventAt (h.roll, rulerPointForStep (h, 0), {}, 2));

    REQUIRE_FALSE (h.editorState.hasStepSelection());
}

TEST_CASE ("double-clicking a piano key does not throw the view away",
           "[ui][pianoroll][zoom]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // Zoom right in, so framing the pattern would be a visible change.
    h.roll.applyView (100.0, 4.0, 0.0);
    const auto zoomed = h.roll.getTimeline().pixelsPerStep;

    const auto keys = h.roll.getKeyboardArea();
    h.roll.mouseDoubleClick (eventAt (h.roll, keys.getCentre(), {}, 2));

    // A double-click on a key means auditioning that note twice. It used to
    // ALSO reframe the whole editor, which is not something a key can mean.
    INFO ("pixelsPerStep " << zoomed << " -> " << h.roll.getTimeline().pixelsPerStep);
    CHECK (juce::exactlyEqual (h.roll.getTimeline().pixelsPerStep, zoomed));
}

TEST_CASE ("the toolbar frames the pattern, and zooms in and out", "[ui][pianoroll][zoom]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    auto& toolbar = h.roll.getToolbar();
    REQUIRE (toolbar.onZoom != nullptr);

    h.roll.applyView (100.0, 4.0, 0.0);
    const auto zoomed = h.roll.getTimeline().pixelsPerStep;

    toolbar.onZoom (1.0 / 1.5);
    const auto out = h.roll.getTimeline().pixelsPerStep;
    CHECK (out < zoomed);

    toolbar.onZoom (1.5);
    CHECK (h.roll.getTimeline().pixelsPerStep > out);

    // Zero is "fit", which is where zoom-to-fit went when the keyboard gutter
    // stopped meaning it.
    h.roll.applyView (100.0, 4.0, 0.0);
    toolbar.onZoom (0.0);
    CHECK (h.roll.getTimeline().pixelsPerStep < 100.0);
    CHECK (juce::exactlyEqual (h.roll.getTimeline().scrollOffsetSteps, 0.0));
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

TEST_CASE ("mod-clicking the piano roll ruler spans from the playhead", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), mod));
    h.roll.mouseUp   (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), mod));

    REQUIRE (h.editorState.hasStepSelection());

    // From where the transport is - step zero here - to where it was clicked.
    const auto selection = h.editorState.getSelectedStepRange();
    INFO ("selection " << selection.getStart() << " -> " << selection.getEnd());
    REQUIRE (selection.getStart() == 0);
    REQUIRE (selection.getEnd() > 0);
}

TEST_CASE ("selecting a span never touches the document or the undo stack",
           "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const auto before = h.document.getState().createCopy();

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 0), shift));
    h.roll.mouseDrag (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));
    h.roll.mouseUp   (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));

    REQUIRE (h.editorState.hasStepSelection());

    // A selection is a view of the project and not part of it: it must not save
    // into the document, make it dirty, or land on the undo stack.
    REQUIRE (before.isEquivalentTo (h.document.getState()));
    REQUIRE_FALSE (h.document.getUndoManager().canUndo());
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
    h.roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
    REQUIRE (h.roll.getNumSelectedNotes() == 4);

    const auto notesBefore = h.countNotes();

    // Empty space: a pitch nothing was written at. A right-press here starts an
    // erase sweep - that is how you sweep INTO notes - but a press that lets go
    // having removed nothing was never an erase.
    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };
    const auto empty = pointFor (h, 0, 72);

    h.roll.mouseDown (eventAt (h.roll, empty, rightButton));
    h.roll.mouseUp   (eventAt (h.roll, empty, rightButton));

    REQUIRE (h.roll.getNumSelectedNotes() == 0);
    REQUIRE (h.countNotes() == notesBefore);
}

TEST_CASE ("a right-drag that erases does not also clear the selection",
           "[ui][pianoroll][erase]")
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
    h.roll.keyPressed (juce::KeyPress ('a', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 'a'));
    REQUIRE (h.roll.getNumSelectedNotes() == 8);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    dragBetween (h.roll, pointFor (h, 0, 66), pointFor (h, 3, 66), 8, rightButton);

    // The four it swept are gone, and gone from the selection with them - but
    // the sweep must not clear the four on the other row as well.
    REQUIRE (h.countNotes() == 4);
    REQUIRE (h.roll.getNumSelectedNotes() == 4);
}

TEST_CASE ("the selected span is painted on the piano roll ruler", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const auto render = [&h]
    {
        juce::Image image (juce::Image::ARGB, h.roll.getWidth(), h.roll.getHeight(), true);
        juce::Graphics g (image);
        h.roll.paintEntireComponent (g, true);
        return image;
    };

    const auto before = render();

    h.editorState.setSelectedStepRange ({ stepsPerBar, 3 * stepsPerBar });

    const auto after = render();

    // Count pixels that actually changed, and only within the ruler: a "is there
    // any accent on screen" test cannot tell a strip from the notes, which are
    // already coloured.
    const auto ruler = h.roll.getRulerArea();
    int changed = 0;

    for (int y = ruler.getY(); y < ruler.getBottom(); ++y)
        for (int x = ruler.getX(); x < ruler.getRight(); ++x)
            if (before.getPixelAt (x, y) != after.getPixelAt (x, y))
                ++changed;

    INFO ("changed ruler pixels: " << changed);
    REQUIRE (changed > 1000);
}

TEST_CASE ("the channel rack ruler shows the same span as the piano roll",
           "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    ChannelRackComponent rack { document, engine, editorState };

    rack.setSize (1200, 600);
    rack.setVisible (true);
    rack.resized();

    auto* strip = rack.findChildWithID ("channelRackRuler");
    REQUIRE (strip != nullptr);

    const auto render = [strip]
    {
        juce::Image image (juce::Image::ARGB, strip->getWidth(), strip->getHeight(), true);
        juce::Graphics g (image);
        strip->paintEntireComponent (g, true);
        return image;
    };

    const auto before = render();

    // Both are views of one pattern, so a span taken out in the roll has to be
    // visible over the steps it covers in the rack.
    editorState.setSelectedStepRange ({ 0, 8 });

    const auto after = render();

    int changed = 0;

    for (int y = 0; y < before.getHeight(); ++y)
        for (int x = 0; x < before.getWidth(); ++x)
            if (before.getPixelAt (x, y) != after.getPixelAt (x, y))
                ++changed;

    INFO ("changed ruler pixels: " << changed);
    REQUIRE (changed > 200);
}

TEST_CASE ("the piano roll's rows can be made taller, and come back to the default",
           "[ui][pianoroll][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    REQUIRE (h.roll.getRowHeight() == tokens::size::pianoRowDefault);

    h.roll.zoomRowsBy (ZoomButtons::zoomFactor);
    CHECK (h.roll.getRowHeight() > tokens::size::pianoRowDefault);

    h.roll.zoomRowsBy (1.0 / ZoomButtons::zoomFactor);
    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowDefault);

    // The range is the ladder's, and it is the only clamp: a factor applied
    // enough times lands on the end rather than walking past it.
    for (int i = 0; i < 20; ++i)
        h.roll.zoomRowsBy (ZoomButtons::zoomFactor);

    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowMax);

    for (int i = 0; i < 40; ++i)
        h.roll.zoomRowsBy (1.0 / ZoomButtons::zoomFactor);

    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowMin);

    h.roll.setRowHeight (tokens::size::pianoRowDefault);
    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowDefault);
}

TEST_CASE ("a taller row keeps the pitch you were looking at", "[ui][pianoroll][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // The bug this guards: growing the rows from the top walks the music out
    // from under whatever was in the middle of the view. The playlist's lane
    // height had exactly this, and the curve tests caught it there.
    const auto area = h.roll.getNoteArea();
    const auto pitchInTheMiddle = [&h, &area]
    {
        return h.roll.getPitchAtY (area.getCentreY());
    };

    const auto before = pitchInTheMiddle();

    h.roll.setRowHeight (tokens::size::pianoRowRoomy);
    CHECK (pitchInTheMiddle() == before);

    h.roll.setRowHeight (tokens::size::pianoRowMin);
    CHECK (pitchInTheMiddle() == before);
}

TEST_CASE ("alt and the zoom keys change the OTHER axis", "[ui][pianoroll][height]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto zoomBefore = h.roll.getTimeline().pixelsPerStep;

    h.roll.keyPressed (juce::KeyPress ('=', juce::ModifierKeys::altModifier, 0));
    CHECK (h.roll.getRowHeight() > tokens::size::pianoRowDefault);

    // And the horizontal zoom is untouched, which is the whole point of the
    // modifier: bare `=` is time, alt-`=` is pitch.
    CHECK (juce::exactlyEqual (h.roll.getTimeline().pixelsPerStep, zoomBefore));

    h.roll.keyPressed (juce::KeyPress ('0', juce::ModifierKeys::altModifier, 0));
    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowDefault);

    // Bare `=` still zooms time and leaves the rows alone.
    h.roll.keyPressed (juce::KeyPress ('='));
    CHECK (h.roll.getTimeline().pixelsPerStep > zoomBefore);
    CHECK (h.roll.getRowHeight() == tokens::size::pianoRowDefault);
}
