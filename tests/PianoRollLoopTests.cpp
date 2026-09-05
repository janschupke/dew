// The span selected on the ruler, the zoom it is read at, and the row height.
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

constexpr int stepsPerBar = 16; // the default project: 4 steps per beat, 4 beats

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
    h.roll.mouseUp (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));

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
    h.roll.mouseUp (eventAt (h.roll, rulerPointForStep (h, 8), {}, 1, true));

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
    h.roll.mouseUp (eventAt (h.roll, at, shift));

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

TEST_CASE ("double-clicking a piano key does not throw the view away", "[ui][pianoroll][zoom]")
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

TEST_CASE ("mod-clicking the piano roll ruler spans from the playhead", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), mod));
    h.roll.mouseUp (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), mod));

    REQUIRE (h.editorState.hasStepSelection());

    // From where the transport is - step zero here - to where it was clicked.
    const auto selection = h.editorState.getSelectedStepRange();
    INFO ("selection " << selection.getStart() << " -> " << selection.getEnd());
    REQUIRE (selection.getStart() == 0);
    REQUIRE (selection.getEnd() > 0);
}

TEST_CASE ("selecting a span never touches the document or the undo stack", "[ui][pianoroll][loop]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;
    makeFourBars (h);

    const auto before = h.document.getState().createCopy();

    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
    h.roll.mouseDown (eventAt (h.roll, rulerPointForStep (h, 0), shift));
    h.roll.mouseDrag (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));
    h.roll.mouseUp (eventAt (h.roll, rulerPointForStep (h, 2 * stepsPerBar), shift, 1, true));

    REQUIRE (h.editorState.hasStepSelection());

    // A selection is a view of the project and not part of it: it must not save
    // into the document, make it dirty, or land on the undo stack.
    REQUIRE (before.isEquivalentTo (h.document.getState()));
    REQUIRE_FALSE (h.document.getUndoManager().canUndo());
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

TEST_CASE ("the channel rack ruler shows the same span as the piano roll", "[ui][pianoroll][loop]")
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
    const auto pitchInTheMiddle = [&h, &area] { return h.roll.getPitchAtY (area.getCentreY()); };

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

TEST_CASE ("the pointer says what a note will do", "[ui][pianoroll][cursor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    RollHarness h;

    const auto cursorAt = [&h] (juce::Point<int> at)
    {
        h.roll.mouseMove (eventAt (h.roll, at));
        return h.roll.getMouseCursor();
    };

    // Written through the component, so the note lands where the roll would
    // paint it rather than where the test guessed.
    const auto noteArea = h.roll.getNoteArea();
    const auto placed = noteArea.getCentre();

    clickAndRelease (h.roll, placed);
    REQUIRE (h.countNotes() > 0);

    juce::ValueTree note;

    for (const auto& child : h.pattern())
        if (child.hasType (ids::NOTE))
            note = child;

    REQUIRE (note.isValid());
    const auto bounds = h.roll.getBoundsForNote (note);

    // A note's BODY is what a person drags, and it had no cursor: only the
    // resize edge said anything.
    CHECK (
        cursorAt ({ (int) (bounds.getX() + bounds.getWidth() * 0.3f), (int) bounds.getCentreY() })
        == cursor::move);

    CHECK (cursorAt ({ (int) bounds.getRight() - 2, (int) bounds.getCentreY() })
           == cursor::resizeX);

    // Control case: empty grid is not draggable. Without it the two above would
    // pass on a roll that returned `move` everywhere.
    CHECK (cursorAt ({ noteArea.getRight() - 4, noteArea.getY() + 4 }) == cursor::idle);

    CHECK (cursorAt (h.roll.getKeyboardArea().getCentre()) == cursor::clickable);
    CHECK (cursorAt (h.roll.getRulerArea().getCentre()) == cursor::clickable);
    // The velocity lane says three different things, and it used to say one.
    // Every pixel of it showed cursor::value - which IS UpDownResizeCursor - so
    // the pointer offered to make the lane taller everywhere, including where
    // there was nothing under it at all and the lane could not be resized.
    const auto lane = h.roll.getVelocityArea();

    // A bar: the note above was written at the note area's centre, so its bar
    // is at the same x.
    CHECK (cursorAt ({ (int) bounds.getX() + 2, lane.getCentreY() }) == cursor::value);

    // Beside it: nothing to drag, so nothing offered. This is the assertion the
    // whole complaint reduces to.
    CHECK (cursorAt ({ lane.getRight() - 4, lane.getCentreY() }) == cursor::idle);

    // And the lane's own top edge, which is the one place a vertical drag DOES
    // resize something.
    CHECK (cursorAt ({ lane.getCentreX(), lane.getY() + 1 }) == cursor::resizeY);

    // A tool outranks what is under the pointer.
    h.roll.getToolbar().setTool (RollTool::slice);
    CHECK (
        cursorAt ({ (int) (bounds.getX() + bounds.getWidth() * 0.3f), (int) bounds.getCentreY() })
        == cursor::nib);
    h.roll.getToolbar().setTool (RollTool::select);

    h.roll.mouseMove (eventAt (h.roll, { (int) bounds.getRight() - 2, (int) bounds.getCentreY() }));
    REQUIRE (h.roll.getMouseCursor() == cursor::resizeX);

    h.roll.mouseExit (eventAt (h.roll, { -1, -1 }));
    CHECK (h.roll.getMouseCursor() == cursor::idle);
}
