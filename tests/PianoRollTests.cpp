#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/PianoRollComponent.h"

using namespace dew;

namespace
{

/** Everything a piano roll needs, laid out and visible, so gestures can be
    driven at it directly. Headless: no window, no display.
*/
struct RollHarness
{
    RollHarness (int width = 1200, int height = 700)
    {
        document.setState (ProjectFactory::createDefault(), true);
        roll.setSize (width, height);
        roll.setVisible (true);
        roll.refresh();
        roll.resized();

        // A fixed frame, so every test below aims at the same coordinates
        // whatever the roll would have chosen to open on.
        roll.centreOnPitch (66);
    }

    juce::ValueTree pattern() { return ProjectEdits::findPattern (document.getState(), 1); }

    int countNotes()
    {
        int n = 0;

        for (const auto& child : pattern())
            if (child.hasType (ids::NOTE))
                ++n;

        return n;
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    PianoRollComponent roll { document, engine, editorState };
};

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                          juce::ModifierKeys mods = juce::ModifierKeys(), int clickCount = 1)
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, mods,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount, false };
}

void clickAndRelease (juce::Component& c, juce::Point<int> at, juce::ModifierKeys mods = juce::ModifierKeys())
{
    c.mouseDown (eventAt (c, at, mods));
    c.mouseUp (eventAt (c, at, mods));
}

/** Where in the component a given step and pitch land.

    Asks the roll where it would paint a note there, rather than recomputing the
    layout - otherwise a layout change leaves these tests clicking confidently
    into the wrong place and still passing.
*/
juce::Point<int> pointFor (RollHarness& h, int step, int pitch)
{
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addNote (h.pattern(), 1, step, 1, pitch, 1.0f, &scratch);
    const auto bounds = h.roll.getBoundsForNote (probe);
    ProjectEdits::removeNote (h.pattern(), probe, &scratch);

    // A test aiming outside the visible area is a broken test, not a finding.
    const auto point = juce::Point<int> ((int) (bounds.getX() + bounds.getWidth() * 0.4f),
                                         (int) bounds.getCentreY());
    REQUIRE (h.roll.getNoteArea().contains (point));
    return point;
}

} // namespace

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

    document.setState (ProjectFactory::createDemo(), true);

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
    const juce::Colour channelColour (0xffe4572e);

    int bleeding = 0;

    for (int y = keys.getY(); y < keys.getBottom(); y += 2)
        for (int x = keys.getX(); x < keys.getRight() - 1; x += 2)
        {
            const auto pixel = image.getPixelAt (x, y);

            if (std::abs ((int) pixel.getRed()   - (int) channelColour.getRed())   < 24
             && std::abs ((int) pixel.getGreen() - (int) channelColour.getGreen()) < 24
             && std::abs ((int) pixel.getBlue()  - (int) channelColour.getBlue())  < 24)
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
