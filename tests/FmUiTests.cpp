// The FM matrix's face: the fourth segment, and the twelve cells behind it.
//
// The engine tests next door prove what the numbers do. These prove that the
// panel writes them onto the right node, that switching to the matrix takes
// every one-slot control off the screen, and that the section asks its host for
// the height the matrix actually needs.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/EditorState.h"
#include "ui/OscillatorSection.h"

#include "model/GeneratorCatalog.h"
#include "ui/design/Tokens.h"

#include "FixtureProject.h"
#include "OscillatorHarness.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

namespace
{

struct FmHarness
{
    FmHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        section.setSize (300, OscillatorSection::heightFor (false, /*lfoOpen*/ false));
        section.setVisible (true);
        section.setOwner (channel().getChildWithName (ids::INSTRUMENT));
    }

    juce::ValueTree channel()
    {
        return firstChannel (document.getState());
    }
    juce::ValueTree slot (int i)
    {
        return ProjectEdits::oscillatorAt (channel(), i);
    }

    /** Selects the matrix and gives the section the height it then asks for.

        EditorState is a ChangeBroadcaster, so its callback is asynchronous and
        there is no message loop here - the pump is what makes the assertions
        that follow hold for the right reason.
    */
    void showMatrix()
    {
        section.selectSlot (OscillatorSection::fmTabIndex);
        editorState.dispatchPendingMessages();
        section.setSize (300, section.getRequiredHeight());
    }

    ProjectDocument document;
    EditorState editorState;
    OscillatorSection section { document, editorState };
};

} // namespace

TEST_CASE ("the selector carries a segment for the matrix as well as the slots", "[ui][fm]")
{
    FmHarness h;

    // Four segments for three slots, and the fourth is not one of them: it
    // answers to fmTabIndex and shows no sounding/silent dot.
    REQUIRE (h.section.getNumSlots() == kMaxOscillators);
    REQUIRE (OscillatorSection::fmTabIndex == kMaxOscillators);

    auto& fmButton = h.section.getSlotButton (OscillatorSection::fmTabIndex);
    REQUIRE (fmButton.isVisible());
    REQUIRE (fmButton.getTooltip().isNotEmpty());

    REQUIRE (h.section.getSelectedSlot() == 0);

    h.showMatrix();
    REQUIRE (h.section.getSelectedSlot() == OscillatorSection::fmTabIndex);
}

TEST_CASE ("the matrix replaces every control that belongs to one slot", "[ui][fm]")
{
    FmHarness h;

    // The control case: they are all on screen before the matrix is.
    for (auto* control : h.section.slotControls())
    {
        INFO (control->getComponentID());
        REQUIRE (control->isVisible());
    }

    h.showMatrix();

    REQUIRE (h.section.getFmMatrix().isVisible());

    for (auto* control : h.section.slotControls())
    {
        INFO (control->getComponentID());
        REQUIRE_FALSE (control->isVisible());
    }

    // And back, so the face is a view rather than a one-way door.
    h.section.selectSlot (0);
    h.editorState.dispatchPendingMessages();

    REQUIRE_FALSE (h.section.getFmMatrix().isVisible());

    for (auto* control : h.section.slotControls())
        REQUIRE (control->isVisible());
}

TEST_CASE ("a cell writes onto its own slot and no other", "[ui][fm]")
{
    FmHarness h;
    h.showMatrix();

    auto& matrix = h.section.getFmMatrix();

    // Row 2, the column that routes into slot 1. A row is a fixed slot here,
    // unlike every other control in the section, which writes the selected one.
    matrix.cellAt (1, 0).setValue (0.75, juce::sendNotificationSync);

    REQUIRE (juce::exactlyEqual ((double) h.slot (1)[ids::fmTo1], 0.75));

    for (const auto row : { 0, 2 })
    {
        INFO ("slot " << row);
        REQUIRE (juce::exactlyEqual ((double) h.slot (row).getProperty (ids::fmTo1, 0.0), 0.0));
    }

    // The output column, which is the one cell of a row that defaults to full.
    REQUIRE (juce::exactlyEqual ((double) h.slot (0).getProperty (ids::fmOut, 1.0), 1.0));

    matrix.cellAt (0, 3).setValue (0.25, juce::sendNotificationSync);
    REQUIRE (juce::exactlyEqual ((double) h.slot (0)[ids::fmOut], 0.25));
}

TEST_CASE ("a whole cell gesture is one undo step", "[ui][fm]")
{
    FmHarness h;
    h.showMatrix();

    auto& cell = h.section.getFmMatrix().cellAt (2, 2);
    auto& undo = h.document.getUndoManager();

    undo.clearUndoHistory();

    cell.onEditStart();
    cell.setValue (0.2, juce::sendNotificationSync);
    cell.setValue (0.4, juce::sendNotificationSync);
    cell.setValue (0.6, juce::sendNotificationSync);
    cell.onEditEnd();

    REQUIRE (juce::exactlyEqual ((double) h.slot (2)[ids::fmTo3], 0.6));

    undo.undo();

    // One step for the drag, not three. A knob dragged across its travel that
    // needed thirty undos to put back is the failure this guards.
    REQUIRE (juce::exactlyEqual ((double) h.slot (2).getProperty (ids::fmTo3, 0.0), 0.0));
}

TEST_CASE ("the matrix follows the document", "[ui][fm]")
{
    FmHarness h;
    h.showMatrix();

    ProjectEdits::setProperty (h.slot (0), ids::fmTo2, 0.4, nullptr, "test", false);

    REQUIRE (juce::exactlyEqual (h.section.getFmMatrix().cellAt (0, 1).getValue(), 0.4));
}

TEST_CASE ("the section asks for the height the matrix needs", "[ui][fm][reflow]")
{
    FmHarness h;

    const auto slotHeight = h.section.getRequiredHeight();

    h.showMatrix();

    const auto fmHeight = h.section.getRequiredHeight();
    REQUIRE (fmHeight != slotHeight);
    REQUIRE (fmHeight >= FmMatrixPanel::preferredHeight);

    // Everything laid out inside the section's own edges, which is the defect
    // a face that forgot to move the height budget actually produces.
    auto& matrix = h.section.getFmMatrix();
    REQUIRE (h.section.getLocalBounds().contains (matrix.getBounds()));

    for (int row = 0; row < kMaxOscillators; ++row)
    {
        for (int column = 0; column < FmMatrixPanel::numColumns; ++column)
        {
            INFO ("cell " << row << "," << column);

            const auto& cell = matrix.cellAt (row, column);
            REQUIRE (cell.getWidth() > 0);
            REQUIRE (matrix.getLocalBounds().contains (cell.getBounds()));
        }
    }
}

TEST_CASE ("every cell says what it is, and says which row it is on", "[ui][fm][a11y]")
{
    FmHarness h;
    h.showMatrix();

    auto& matrix = h.section.getFmMatrix();
    juce::StringArray seen;

    for (int row = 0; row < kMaxOscillators; ++row)
    {
        for (int column = 0; column < FmMatrixPanel::numColumns; ++column)
        {
            const auto tip = matrix.cellAt (row, column).getTooltip();

            INFO ("cell " << row << "," << column);
            REQUIRE (tip.isNotEmpty());

            seen.addIfNotAlreadyThere (tip);
        }
    }

    // Twelve DIFFERENT sentences. Three rows share four catalog rows, so a cell
    // taking the catalog's own name would leave three of them saying the same
    // thing - and that sentence is the accessible name as well as the tooltip.
    REQUIRE (seen.size() == kMaxOscillators * FmMatrixPanel::numColumns);
}

TEST_CASE ("each matrix row shows the waveform of the oscillator it is", "[ui][fm]")
{
    /*  The gutter used to hold a bare digit, which said which ROW you were on
        and nothing about which oscillator that was or what it sounded like.
        The matrix is the one face in the section showing all three slots at
        once, so it is the one place that can show three waveforms side by side.

        Measured as ink in the gutter rather than by naming a glyph: which path
        a waveform maps to is GlyphTests' subject, and asserting it again here
        would only pin this test to that mapping.
    */
    const juce::ScopedJuceInitialiser_GUI juceInit;

    FmHarness h;
    h.showMatrix();

    auto& matrix = h.section.getFmMatrix();

    // The gutter, which is what resized() takes off the left before planning
    // the knob columns.
    const juce::Rectangle<int> gutter (0, 0, tokens::size::fmRowLabel, matrix.getHeight());

    const auto inkIn = [&matrix, gutter]
    {
        const auto image = render (matrix);
        auto marked = 0;

        for (int y = gutter.getY(); y < juce::jmin (gutter.getBottom(), image.getHeight()); ++y)
            for (int x = gutter.getX(); x < juce::jmin (gutter.getRight(), image.getWidth()); ++x)
                if (image.getPixelAt (x, y).getBrightness() > 0.4f)
                    ++marked;

        return marked;
    };

    const auto sine = inkIn();

    // Three rows, each with a mark and a word, is a lot more than three digits
    // were - but the number that matters is that something is there at all.
    REQUIRE (sine > 0);

    // Change what the first slot plays and the gutter has to follow. `wave`
    // lives on the CLASSIC node UNDER the slot, which is the reason the panel's
    // listener cannot gate on the slot's own type.
    //
    // This covers the PAINTING and not the listener: render() paints on demand,
    // so it would draw the new waveform even if nothing had asked it to. What
    // the listener adds is that the screen follows without a render being
    // asked for, and a headless component has no repaint to observe.
    auto classic = generatorNodeFor (h.slot (0), ids::wave);
    REQUIRE (classic.isValid());

    ProjectEdits::setProperty (classic, ids::wave, "square", nullptr, "wave");

    INFO ("gutter ink as sine: " << sine << ", as square: " << inkIn());
    CHECK (inkIn() != sine);
}
