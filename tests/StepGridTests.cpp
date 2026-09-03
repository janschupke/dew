#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/EditorState.h"
#include "ui/design/Tokens.h"
#include "ui/StepGridComponent.h"

using namespace dew;

namespace
{

struct GridHarness
{
    GridHarness (int width = 1200, int height = 400)
    {
        document.setState (ProjectFactory::createDefault(), true);
        grid.setSize (width, height);
        grid.setVisible (true);
        grid.resized();
    }

    juce::ValueTree pattern() { return ProjectEdits::findPattern (document.getState(), 1); }

    /** The channel drawn on row 0. Channels are direct children of PROJECT and
        the grid draws them in tree order, so the first one is the top row.
    */
    juce::ValueTree firstChannel() { return document.getState().getChildWithName (ids::CHANNEL); }

    void setPatternLength (int steps)
    {
        pattern().setProperty (ids::lengthSteps, steps, nullptr);
        grid.resized();
    }

    juce::Image render()
    {
        juce::Image image (juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true);
        juce::Graphics g (image);
        grid.paintEntireComponent (g, true);
        return image;
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    StepGridComponent grid { document, engine, editorState };
};

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                          juce::ModifierKeys mods = juce::ModifierKeys())
{
    const auto position = local.toFloat();

    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, mods,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             1, false };
}

} // namespace

TEST_CASE ("the step grid keeps drawing past the end of a short pattern", "[stepgrid][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    // updateZoom caps a cell at 64px, so four steps occupy 256 of 1200 and the
    // rest of the panel is past the pattern end.
    h.setPatternLength (4);

    const auto endX = (int) h.grid.getTimeline().xForStep (4.0);
    const auto rowsHeight = h.grid.getRowsHeight();

    REQUIRE (rowsHeight > 40);
    REQUIRE (endX < h.grid.getWidth() - 200);

    const auto image = h.render();

    const auto columnMean = [&image, rowsHeight] (int x)
    {
        double total = 0.0;

        for (int y = 2; y < rowsHeight - 2; ++y)
            total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, rowsHeight - 4);
    };

    juce::Array<double> beyond;

    for (int x = endX + 4; x < h.grid.getWidth() - 2; ++x)
        beyond.add (columnMean (x));

    REQUIRE (beyond.size() > 100);

    auto sorted = beyond;
    sorted.sort();
    const auto background = sorted[sorted.size() / 2];

    int verticalLines = 0;

    for (auto value : beyond)
        if (value > background * 1.15 + 0.002)
            ++verticalLines;

    INFO ("vertical grid lines past the pattern end: " << verticalLines);
    REQUIRE (verticalLines >= 3);

    // And the end itself is marked, so "outside the pattern" is still legible
    // now that it is no longer blanked out.
    const auto meanOver = [&image] (juce::Rectangle<int> area)
    {
        double total = 0.0;
        int counted = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x, ++counted)
                total += image.getPixelAt (x, y).getBrightness();

        return total / juce::jmax (1, counted);
    };

    const auto ruleMean = meanOver ({ endX - 1, 2, 3, rowsHeight - 4 });
    const auto nearbyMean = meanOver ({ endX + 20, 2, 3, rowsHeight - 4 });

    INFO ("rule " << ruleMean << " vs nearby " << nearbyMean);
    REQUIRE (ruleMean > nearbyMean * 2.0);
}

TEST_CASE ("the region below the last channel stays inert", "[stepgrid][grid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto rowsHeight = h.grid.getRowsHeight();

    // Deliberately NOT the same treatment as past the pattern end. There are no
    // rows below the last channel to carry on into, so continuing the grid
    // there would promise steps that do not exist.
    REQUIRE (rowsHeight < h.grid.getHeight() - 40);

    const auto image = h.render();

    double belowTotal = 0.0;
    int counted = 0;

    for (int y = rowsHeight + 8; y < h.grid.getHeight() - 4; ++y)
        for (int x = 4; x < h.grid.getWidth() - 4; x += 3, ++counted)
            belowTotal += image.getPixelAt (x, y).getBrightness();

    const auto belowMean = belowTotal / juce::jmax (1, counted);

    INFO ("mean brightness below the last row: " << belowMean);
    REQUIRE (belowMean > 0.0);
    REQUIRE (belowMean < 0.12);
}

namespace
{

/** The centre of a cell, asked of the grid's own timeline rather than
    recomputed, so a layout change breaks the test instead of silently moving
    it somewhere harmless.
*/
juce::Point<int> cellCentre (GridHarness& h, int step, int row)
{
    const auto x = (int) (h.grid.getTimeline().xForStep ((double) step + 0.5));
    const auto y = row * tokens::size::rowHeight + tokens::size::rowHeight / 2;

    REQUIRE (x > 0);
    REQUIRE (y < h.grid.getRowsHeight());

    return { x, y };
}

int stepsLitOnRow (GridHarness& h, int channelId)
{
    int lit = 0;

    for (const auto& child : h.pattern())
        if (child.hasType (ids::NOTE) && (int) child[ids::ch] == channelId)
            ++lit;

    return lit;
}

} // namespace

TEST_CASE ("a right-drag on the step grid erases rather than adding", "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();

    REQUIRE (channel.isValid());
    REQUIRE (h.grid.getNumRows() > 0);

    const auto channelId = (int) channel[ids::id];

    juce::UndoManager setup;

    for (int step = 0; step < 8; ++step)
        if (! ProjectEdits::findNoteAtStep (h.pattern(), channelId, step).isValid())
            ProjectEdits::addNote (h.pattern(), channelId, step, 1,
                                   (int) channel[ids::basePitch], 1.0f, &setup);

    const auto before = stepsLitOnRow (h, channelId);
    REQUIRE (before >= 8);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0), rightButton));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 7, 0), rightButton));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 7, 0), rightButton));

    INFO ("steps left on the row: " << stepsLitOnRow (h, channelId));
    REQUIRE (stepsLitOnRow (h, channelId) == before - 8);
}

TEST_CASE ("a right-drag starting on an empty cell still erases", "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];

    juce::UndoManager setup;

    // Clear the row, then light only the far half.
    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    for (int step = 8; step < 12; ++step)
        ProjectEdits::addNote (h.pattern(), channelId, step, 1,
                               (int) channel[ids::basePitch], 1.0f, &setup);

    REQUIRE (stepsLitOnRow (h, channelId) == 4);

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    // Step 0 is empty. Without a modifier check this press decided "the first
    // cell is empty, so this drag ADDS" and filled the row instead.
    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0), rightButton));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 11, 0), rightButton));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 11, 0), rightButton));

    INFO ("steps left on the row: " << stepsLitOnRow (h, channelId));
    REQUIRE (stepsLitOnRow (h, channelId) == 0);
}

TEST_CASE ("a fast sweep fills the cells between two drag samples", "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];

    juce::UndoManager setup;

    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    REQUIRE (stepsLitOnRow (h, channelId) == 0);

    // One press and one drag report, twelve cells apart. Painting only where
    // the pointer was reported would light two cells and leave ten dark.
    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0)));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 11, 0)));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 11, 0)));

    INFO ("steps lit by a two-sample sweep: " << stepsLitOnRow (h, channelId));
    REQUIRE (stepsLitOnRow (h, channelId) == 12);
}

TEST_CASE ("a left-drag on the step grid still paints", "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];

    juce::UndoManager setup;

    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    const juce::ModifierKeys left { juce::ModifierKeys::leftButtonModifier };

    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0), left));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 3, 0), left));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 3, 0), left));

    REQUIRE (stepsLitOnRow (h, channelId) == 4);
}

TEST_CASE ("a left click on a lit step keeps it, and selects its channel",
           "[stepgrid][erase]")
{
    // The first cell used to decide whether the whole drag added or removed, so
    // pressing a lit step turned the gesture into an erase - which made the
    // ordinary way of looking at a pattern, clicking around it, delete the
    // thing that was clicked.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];

    juce::UndoManager setup;

    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    ProjectEdits::addNote (h.pattern(), channelId, 4, 1, (int) channel[ids::basePitch],
                           1.0f, &setup);

    REQUIRE (stepsLitOnRow (h, channelId) == 1);

    h.editorState.setSelectedChannelId (-1);

    const juce::ModifierKeys left { juce::ModifierKeys::leftButtonModifier };

    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 4, 0), left));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 4, 0), left));

    CHECK (stepsLitOnRow (h, channelId) == 1);
    CHECK (h.editorState.getSelectedChannelId() == channelId);
}

TEST_CASE ("a left drag over lit steps fills the gaps and leaves them alone",
           "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];
    const auto pitch = (int) channel[ids::basePitch];

    juce::UndoManager setup;

    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    ProjectEdits::addNote (h.pattern(), channelId, 0, 1, pitch, 1.0f, &setup);
    ProjectEdits::addNote (h.pattern(), channelId, 3, 1, pitch, 1.0f, &setup);

    const juce::ModifierKeys left { juce::ModifierKeys::leftButtonModifier };

    // Starting ON a lit step, which used to mean "erase everything I touch".
    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0), left));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 3, 0), left));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 3, 0), left));

    CHECK (stepsLitOnRow (h, channelId) == 4);
}

TEST_CASE ("right-drag is still the way a step is taken back", "[stepgrid][erase]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto channel = h.firstChannel();
    const auto channelId = (int) channel[ids::id];
    const auto pitch = (int) channel[ids::basePitch];

    juce::UndoManager setup;

    for (int step = 0; step < 16; ++step)
        if (auto existing = ProjectEdits::findNoteAtStep (h.pattern(), channelId, step);
            existing.isValid())
            ProjectEdits::removeNote (h.pattern(), existing, &setup);

    for (int step = 0; step < 4; ++step)
        ProjectEdits::addNote (h.pattern(), channelId, step, 1, pitch, 1.0f, &setup);

    REQUIRE (stepsLitOnRow (h, channelId) == 4);

    const juce::ModifierKeys right { juce::ModifierKeys::rightButtonModifier };

    h.grid.mouseDown (eventAt (h.grid, cellCentre (h, 0, 0), right));
    h.grid.mouseDrag (eventAt (h.grid, cellCentre (h, 3, 0), right));
    h.grid.mouseUp   (eventAt (h.grid, cellCentre (h, 3, 0), right));

    CHECK (stepsLitOnRow (h, channelId) == 0);
}

TEST_CASE ("rewind zeroes the position even when nothing is processing", "[stepgrid][transport]")
{
    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    engine.prepare (44100.0, 512);
    engine.setProject (document.getState());
    engine.setMode (Transport::Mode::pattern);
    engine.play();

    juce::AudioBuffer<float> block (2, 512);

    for (int i = 0; i < 400; ++i)
    {
        block.clear();
        engine.processBlock (block);
    }

    REQUIRE (engine.getPlayheadSteps() > 1.0);

    // Now the callback stops - the device was closed from Audio Settings, or
    // lost. rewindRequested is consumed ONLY inside processBlock, so before
    // this the position would stay wherever it happened to stop, forever, and
    // the transport readout with it.
    engine.stop();
    engine.rewind();

    REQUIRE (juce::exactlyEqual (engine.getPlayheadSteps(), 0.0));
}

TEST_CASE ("the indicator is drawn while stopped, and Stop puts it back", "[stepgrid][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    GridHarness h;

    const auto rowsHeight = h.grid.getRowsHeight();

    /** Where the brightest playhead-coloured column is, or -1. */
    const auto indicatorX = [rowsHeight] (const juce::Image& image, int width)
    {
        int best = -1;
        double bestScore = 0.0;

        for (int x = 0; x < width - 1; ++x)
        {
            double score = 0.0;

            for (int y = 2; y < rowsHeight - 2; ++y)
            {
                const auto pixel = image.getPixelAt (x, y);

                // Playhead yellow: strongly red and green, comparatively weak
                // blue. The grid's own greys are blue-leaning, so this does not
                // pick them up.
                if (pixel.getRed() > 90 && pixel.getGreen() > 70
                    && (float) pixel.getBlue() < (float) pixel.getRed() * 0.7f)
                    score += 1.0;
            }

            if (score > bestScore)
            {
                bestScore = score;
                best = x;
            }
        }

        return bestScore > (double) rowsHeight * 0.5 ? best : -1;
    };

    // Stopped and at zero: there should still be an indicator, at the start.
    // Before this it was gated on isPlaying() and there was none at all.
    const auto atRest = indicatorX (h.render(), h.grid.getWidth());

    INFO ("indicator column while stopped at zero: " << atRest);
    REQUIRE (atRest >= 0);
    REQUIRE (atRest < (int) h.grid.getTimeline().pixelsPerStep);

    // Run the transport far enough to move it, then Stop.
    h.engine.prepare (44100.0, 512);
    h.engine.setProject (h.document.getState());
    h.engine.setMode (Transport::Mode::pattern);
    h.engine.play();

    juce::AudioBuffer<float> block (2, 512);

    for (int i = 0; i < 400; ++i)
    {
        block.clear();
        h.engine.processBlock (block);
    }

    const auto whilePlaying = indicatorX (h.render(), h.grid.getWidth());

    INFO ("indicator column while playing: " << whilePlaying);
    REQUIRE (whilePlaying > atRest + (int) h.grid.getTimeline().pixelsPerStep);

    // Stop and rewind, exactly as the transport bar's Stop button does. The
    // indicator has to be back at the start in the very next frame, without
    // another processBlock and without pressing Play again.
    h.engine.stop();
    h.engine.rewind();

    const auto afterStop = indicatorX (h.render(), h.grid.getWidth());

    INFO ("indicator column after Stop: " << afterStop);
    REQUIRE (afterStop >= 0);
    REQUIRE (afterStop < (int) h.grid.getTimeline().pixelsPerStep);
}

TEST_CASE ("the sequencer zooms, and keeps the zoom it is given", "[ui][stepgrid][zoom]")
{
    // The sequencer had no zoom at all: it fitted itself to the pattern on
    // every layout, so a 128-step pattern was hairlines and a 4-step one was
    // four enormous cells, and there was no way to say otherwise.
    GridHarness h;

    const auto fitted = h.grid.getTimeline().pixelsPerStep;
    REQUIRE (fitted > 0.0);

    h.grid.zoomBy (2.0, 0.0f);
    const auto zoomed = h.grid.getTimeline().pixelsPerStep;

    INFO ("fitted at " << fitted << ", zoomed to " << zoomed);
    CHECK (zoomed > fitted);

    // And it SURVIVES. Auto-fit used to be a law rather than a default, so the
    // next layout - or the next pattern-length change - threw the zoom away.
    h.grid.resized();
    CHECK (juce::approximatelyEqual (h.grid.getTimeline().pixelsPerStep, zoomed));

    h.setPatternLength (64);
    CHECK (juce::approximatelyEqual (h.grid.getTimeline().pixelsPerStep, zoomed));

    // Until it is asked to frame the pattern again.
    h.grid.zoomToFit();
    CHECK (h.grid.getTimeline().pixelsPerStep < zoomed);
}

TEST_CASE ("the sequencer's zoom reaches as far as the other views'", "[ui][stepgrid][zoom]")
{
    // The step grid clamped itself to 18..64 pixels a step while TimelineView -
    // which the playlist and the piano roll use - allows 3..120. Two clamps on
    // one mapping type meant the same pattern could be zoomed further in one
    // editor than in another.
    GridHarness h;

    for (int i = 0; i < 40; ++i)
        h.grid.zoomBy (1.5, 0.0f);

    CHECK (h.grid.getTimeline().pixelsPerStep > 64.0);
    CHECK (h.grid.getTimeline().pixelsPerStep <= TimelineView::maxPixelsPerStep);

    for (int i = 0; i < 80; ++i)
        h.grid.zoomBy (1.0 / 1.5, 0.0f);

    CHECK (h.grid.getTimeline().pixelsPerStep < 18.0);
    CHECK (h.grid.getTimeline().pixelsPerStep >= TimelineView::minPixelsPerStep);
}

TEST_CASE ("the sequencer answers the same zoom keys as every other view", "[ui][stepgrid][zoom]")
{
    // It did not override keyPressed at all, so +, - and 0 did nothing in the
    // tab where they were most useful.
    GridHarness h;

    const auto fitted = h.grid.getTimeline().pixelsPerStep;

    CHECK (h.grid.keyPressed (juce::KeyPress ('=')));
    CHECK (h.grid.getTimeline().pixelsPerStep > fitted);

    CHECK (h.grid.keyPressed (juce::KeyPress ('-')));
    CHECK (juce::approximatelyEqual (h.grid.getTimeline().pixelsPerStep, fitted));

    h.grid.zoomBy (3.0, 0.0f);
    CHECK (h.grid.keyPressed (juce::KeyPress ('0')));
    CHECK (juce::approximatelyEqual (h.grid.getTimeline().pixelsPerStep, fitted));

    // And leaves alone the keys it has nothing to do with, rather than
    // swallowing them from whatever else is listening.
    CHECK_FALSE (h.grid.keyPressed (juce::KeyPress ('1')));
    CHECK_FALSE (h.grid.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
}
