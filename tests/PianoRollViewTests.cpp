// The zoom, the scroll that follows the notes, and the grid under them.
//
// Split out of a PianoRollTests.cpp that had no tag seam left to use - every
// case in it was [ui][pianoroll]. These three are its subjects. The fixture is
// RollHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "model/Meter.h"
#include "ui/ChannelRackComponent.h"
#include "ui/ZoomButtons.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"

#include "FixtureProject.h"
#include "PaintProbe.h"
#include "RollHarness.h"

using namespace dew;
using namespace dew::testing;

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

TEST_CASE ("a trackpad's fragments of a notch add up over the pitch rows", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    // The playlist's lanes and the roll's pitch rows run the same arithmetic -
    // RowView - and this is the second of its two callers. A pitch row starts at
    // 14px, where a fragment of a notch asks for 14.06 and rounds back to 14, so
    // cross-zooming a roll on a trackpad did nothing at all.
    const auto wheel = [] (float deltaY)
    {
        juce::MouseWheelDetails w {};
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = true;
        w.isInertial = false;
        return w;
    };

    const auto area = h.roll.getNoteArea();
    const auto at = juce::Point<int> (area.getCentreX(), area.getCentreY());
    const auto crossZoom = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    const auto before = h.roll.getRowHeight();
    const auto zoomBefore = h.roll.getTimeline().pixelsPerStep;

    for (int i = 0; i < 100; ++i)
        h.roll.mouseWheelMove (eventAt (h.roll, at, crossZoom), wheel (0.002f));

    INFO ("row height went from " << before << " to " << h.roll.getRowHeight());
    CHECK (h.roll.getRowHeight() > before);

    // And it was the OTHER axis throughout: a cross-zoom that also moved time
    // would be the modifier test above failing in a way this one hides.
    CHECK (juce::exactlyEqual (h.roll.getTimeline().pixelsPerStep, zoomBefore));
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

TEST_CASE ("a pinch reads the same modifiers a wheel notch does", "[ui][pianoroll]")
{
    // The roll's half of the playlist case of the same name. Command-shift is
    // "the other axis" for a wheel notch and was not for a pinch, which read no
    // modifiers at all.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto area = h.roll.getNoteArea();
    const auto anchor = juce::Point<int> (area.getCentreX(), area.getCentreY());
    const auto crossZoom = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    const auto rowsBefore = h.roll.getRowHeight();
    const auto timeBefore = h.roll.getTimeline().pixelsPerStep;

    h.roll.mouseMagnify (eventAt (h.roll, anchor, crossZoom), 2.0f);

    INFO ("row " << rowsBefore << " -> " << h.roll.getRowHeight());
    CHECK (h.roll.getRowHeight() > rowsBefore);
    CHECK (juce::exactlyEqual (h.roll.getTimeline().pixelsPerStep, timeBefore));

    // The control case: a bare pinch still zooms time and leaves the rows.
    const auto tallerRows = h.roll.getRowHeight();

    h.roll.mouseMagnify (eventAt (h.roll, anchor), 2.0f);

    CHECK (h.roll.getTimeline().pixelsPerStep > timeBefore);
    CHECK (h.roll.getRowHeight() == tallerRows);
}

TEST_CASE ("the roll follows a song into the pattern it is showing", "[ui][pianoroll]")
{
    /*  The indicator was gated on Transport::Mode::pattern outright, so in song
        mode the roll drew nothing at all - even while the arrangement was
        playing a clip of the very pattern on screen. Following the music into
        the notes being edited meant switching modes and losing your place.

        The answer is the position inside whichever clip of THIS pattern the
        song playhead is in, and nothing when it is in none of them - which is
        the half that keeps this from being a line that is always somewhere.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto stepsPerBar = (double) Meter::of (h.document.getState()).stepsPerBar();

    auto track = ProjectEdits::addPlaylistTrack (h.document.getState(), "Lane", nullptr);
    REQUIRE (track.isValid());

    // The pattern the roll is showing, placed at bar 4 and two bars long.
    ProjectEdits::addClip (track, (int) h.pattern()[ids::id], 4 * 16, 2 * 16, nullptr);

    h.engine.setMode (Transport::Mode::song);

    // Before the clip: nothing to draw.
    h.engine.setPlayheadSteps (2.0 * stepsPerBar);
    CHECK_FALSE (h.roll.playheadInPattern().has_value());

    // A third of the way into the clip's first bar, which is a fractional
    // position on purpose: the pattern-mode views used to truncate to the
    // integer step and this one no longer does.
    const auto into = 4.0 * stepsPerBar + stepsPerBar / 3.0;
    h.engine.setPlayheadSteps (into);

    const auto at = h.roll.playheadInPattern();
    REQUIRE (at.has_value());

    INFO ("song step " << into << " maps to pattern step " << *at);
    CHECK (*at > stepsPerBar / 3.0 - 0.5);
    CHECK (*at < stepsPerBar / 3.0 + 0.5);

    // Past the clip: nothing again.
    h.engine.setPlayheadSteps (8.0 * stepsPerBar);
    CHECK_FALSE (h.roll.playheadInPattern().has_value());

    // And pattern mode is unchanged: the transport IS the pattern there, so
    // every position is inside it.
    h.engine.setMode (Transport::Mode::pattern);
    h.engine.setPlayheadSteps (8.0 * stepsPerBar);

    CHECK (h.roll.playheadInPattern().has_value());
}

TEST_CASE ("the octave rule is drawn under its C, not above it", "[pianoroll][grid]")
{
    /*  The row painter walks rows top to bottom while PITCH decreases, so the
        line it draws at a row's top is the boundary between `pitch + 1` above
        and `pitch` below. Asking `pitch % 12 == 0` there - which is what this
        used to do - selects the boundary between C# and C, and puts the strong
        octave rule ABOVE every C. An octave starts at C, so the rule belongs
        under it.

        Measured on the two candidate pixel rows rather than on a colour,
        because the bug is entirely about WHICH of two lines got the strong
        one and both are drawn either way.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollHarness h { 1200, 700 };

    constexpr int middleC = 60;
    h.roll.centreOnPitch (middleC);

    // Where C4's row is, asked of the roll rather than recomputed - the same
    // reason pointFor does it this way.
    juce::UndoManager scratch;
    auto probe = ProjectEdits::addNote (h.pattern(), 1, 0, 1, middleC, 1.0f, &scratch);
    const auto row = h.roll.getBoundsForNote (probe);
    ProjectEdits::removeNote (h.pattern(), probe, &scratch);

    const auto aboveC = (int) row.getY();
    const auto belowC = (int) row.getBottom();

    // The rows either side really are the ones this is about.
    REQUIRE (h.roll.getPitchAtY (aboveC + 2) == middleC);
    REQUIRE (h.roll.getPitchAtY (belowC + 2) == middleC - 1);

    // Nothing on top of the grid: a note painted across either candidate row
    // would be measured as a line.
    for (auto note = h.pattern().getNumChildren(); --note >= 0;)
        if (const auto child = h.pattern().getChild (note); child.hasType (ids::NOTE))
            ProjectEdits::removeNote (h.pattern(), child, &scratch);

    h.roll.refresh();

    const auto image = render (h.roll);
    const auto notes = h.roll.getNoteArea();

    // Started clear of the left edge, where the playhead has its own column.
    const auto rowMean = [&image, notes] (int y)
    {
        double total = 0.0;
        int counted = 0;

        for (int x = notes.getX() + 20; x < notes.getRight() - 2; ++x, ++counted)
            total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, counted);
    };

    const auto above = rowMean (aboveC);
    const auto below = rowMean (belowC);

    INFO ("row mean above C4 (y " << aboveC << "): " << above << ", below C4 (y " << belowC
                                  << "): " << below);
    CHECK (below > above);
}
