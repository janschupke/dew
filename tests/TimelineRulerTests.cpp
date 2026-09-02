#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorState.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Tokens.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

namespace
{

juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> local,
                          juce::ModifierKeys mods = juce::ModifierKeys(),
                          int clickCount = 1, bool wasDragged = false)
{
    const auto position = local.toFloat();

    // wasDragged is the LAST constructor argument, and the only way a synthetic
    // event can report as a drag: mouseWasDraggedSinceMouseDown asks the mouse
    // source, which nothing in a headless test ever pressed.
    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, mods,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &target, &target,
             juce::Time::getCurrentTime(),
             position,
             juce::Time::getCurrentTime(),
             clickCount, wasDragged };
}

const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier };
const juce::ModifierKeys mod { juce::ModifierKeys::commandModifier };

/** A piano roll sized, laid out and framed on its whole pattern. */
struct RollFixture
{
    RollFixture()
    {
        document.setState (ProjectFactory::createDefault(), true);
        roll.setSize (1200, 700);
        roll.setVisible (true);
        roll.refresh();
        roll.resized();
        roll.zoomToFit();
    }

    int stepsPerBeat() const
    {
        return juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    }

    /** Where a fractional step sits on the ruler. */
    juce::Point<int> rulerAt (double step) const
    {
        const auto strip = roll.getRulerArea();

        return { strip.getX() + (int) roll.getTimeline().xForStep (step), strip.getCentreY() };
    }

    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    PianoRollComponent roll { document, engine, editorState };
};

} // namespace

TEST_CASE ("a ruler click maps to a position, clamped to the material", "[ruler]")
{
    TimelineView timeline;
    timeline.pixelsPerStep = 20.0;
    timeline.scrollOffsetSteps = 0.0;

    const juce::Rectangle<int> strip { 100, 0, 400, tokens::size::rulerHeight };

    // The strip's own origin is step zero, so the gutter to its left does not
    // shift the answer.
    CHECK_THAT (ruler::stepForClick (100, strip, timeline, 32), WithinAbs (0.0, 1e-9));
    CHECK_THAT (ruler::stepForClick (200, strip, timeline, 32), WithinAbs (5.0, 1e-9));
    CHECK_THAT (ruler::stepForClick (300, strip, timeline, 32), WithinAbs (10.0, 1e-9));

    // Scrolling moves what a given x means.
    timeline.scrollOffsetSteps = 4.0;
    CHECK_THAT (ruler::stepForClick (100, strip, timeline, 32), WithinAbs (4.0, 1e-9));

    timeline.scrollOffsetSteps = 0.0;

    // Clamped: dragging off the end of a short pattern parks at its end rather
    // than seeking into space with nothing in it.
    CHECK_THAT (ruler::stepForClick (500, strip, timeline, 8), WithinAbs (8.0, 1e-9));
    CHECK_THAT (ruler::stepForClick (0, strip, timeline, 8), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("clicking the piano roll ruler moves the transport", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    PianoRollComponent roll { document, engine, editorState };

    roll.setSize (1200, 700);
    roll.setVisible (true);
    roll.refresh();
    roll.resized();
    roll.zoomToFit();

    const auto strip = roll.getRulerArea();
    const auto& timeline = roll.getTimeline();

    // Halfway along the ruler, whatever the zoom happens to be.
    const auto x = strip.getX() + strip.getWidth() / 2;
    const auto expected = ruler::stepForClick (x, strip, timeline, 16);

    REQUIRE (expected > 1.0);

    // A press here used to fall through every branch and be dropped by the
    // note-area guard.
    roll.mouseDown (eventAt (roll, { x, strip.getCentreY() }));

    INFO ("expected " << expected << " got " << engine.getPlayheadSteps());
    REQUIRE_THAT (engine.getPlayheadSteps(), WithinAbs (expected, 0.5));

    // And dragging along it scrubs.
    const auto backX = strip.getX() + strip.getWidth() / 4;
    roll.mouseDrag (eventAt (roll, { backX, strip.getCentreY() }));

    REQUIRE_THAT (engine.getPlayheadSteps(),
                  WithinAbs (ruler::stepForClick (backX, strip, timeline, 16), 0.5));
}

TEST_CASE ("clicking the channel rack ruler moves the transport", "[ruler]")
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

    // The channel rack had no ruler at all before this.
    REQUIRE (strip != nullptr);
    REQUIRE (strip->getWidth() > 100);
    REQUIRE (strip->getHeight() == tokens::size::rulerHeight);

    const auto x = strip->getWidth() / 2;
    strip->mouseDown (eventAt (*strip, { x, strip->getHeight() / 2 }));

    INFO ("playhead after clicking the middle of the ruler: " << engine.getPlayheadSteps());
    REQUIRE (engine.getPlayheadSteps() > 5.0);
    REQUIRE (engine.getPlayheadSteps() < 11.0);
}

TEST_CASE ("the channel rack ruler starts where the step columns start", "[ruler]")
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

    // A ruler that does not line up with the grid under it is the classic way
    // to get this wrong, and it is invisible until someone looks closely.
    CHECK (strip->getX() == tokens::size::headerWidth);
    CHECK (strip->getRight() == rack.getWidth());
    CHECK (strip->getY() == 0);
}

TEST_CASE ("clicking the playlist ruler moves the transport, in bars", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    PlaylistComponent playlist { document, engine, editorState };

    playlist.setSize (1200, 600);
    playlist.setVisible (true);
    playlist.resized();

    engine.setMode (Transport::Mode::song);

    const auto stepsPerBar = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]) * 4;

    // Its mouseDown used to bail on anything above rulerHeight, so the third
    // ruler in the app was as inert as the other two.
    const auto strip = playlist.getRulerArea();
    playlist.mouseDown (eventAt (playlist, { strip.getX() + 200, strip.getCentreY() }));

    INFO ("playhead in steps: " << engine.getPlayheadSteps()
          << " (a bar is " << stepsPerBar << " steps)");
    REQUIRE (engine.getPlayheadSteps() > 0.0);

    // Whole bars, because this timeline counts bars.
    const auto bars = engine.getPlayheadSteps() / (double) stepsPerBar;
    REQUIRE_THAT (bars, WithinAbs (std::floor (bars + 0.5), 0.5));
}

TEST_CASE ("seeking silences what was sounding before the jump", "[ruler]")
{
    ProjectDocument document;
    document.setState (ProjectFactory::createDemo(), true);

    AudioEngine engine;
    engine.prepare (44100.0, 512);
    engine.setProject (document.getState());
    engine.setMode (Transport::Mode::song);
    engine.rewind();
    engine.play();

    juce::AudioBuffer<float> block (2, 512);

    const auto renderPeak = [&engine, &block]
    {
        block.clear();
        engine.processBlock (block);
        return block.getMagnitude (0, block.getNumSamples());
    };

    // Run until a block is genuinely loud, so "was sounding" is this block,
    // not the loudest of many.
    float before = 0.0f;

    for (int i = 0; i < 200 && before < 0.05f; ++i)
        before = renderPeak();

    REQUIRE (before >= 0.05f);

    // Jump somewhere the arrangement is not playing. Voices are reset on
    // arrival for the same reason rewind resets them: whatever was sounding
    // has no note-off ahead of it any more, so without the reset it rings
    // straight through the seek at roughly the same level.
    engine.setPlayheadSteps (4000.0);

    const auto after = renderPeak();

    INFO ("block before the seek " << before << ", block after " << after);
    REQUIRE (after < before * 0.5f);
}

// -----------------------------------------------------------------------------
// The gesture every ruler shares. It was written twice - once in the piano roll
// and once in the playlist, against two private enums - and the channel rack
// had none of it at all.

TEST_CASE ("a span dragged on the ruler snaps to a beat, not a bar", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;
    const auto beat = f.stepsPerBeat();

    // The default pattern is a single bar, which is exactly why bar snapping
    // was unusable here: the only span it could express was the whole thing.
    REQUIRE (beat * 4 >= 16);

    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (4.5), shift));
    f.roll.mouseDrag (eventAt (f.roll, f.rulerAt (6.5), shift, 1, true));

    const auto range = f.editorState.getSelectedStepRange();

    INFO ("selected " << range.getStart() << ".." << range.getEnd());
    CHECK (range.getStart() == beat);
    CHECK (range.getEnd() == beat * 2);
    CHECK (range.getLength() == beat);
}

TEST_CASE ("a drag shorter than one beat still selects one", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;

    // Otherwise the strip flickers in and out at the start of every gesture.
    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (4.1), shift));
    f.roll.mouseDrag (eventAt (f.roll, f.rulerAt (4.3), shift, 1, true));

    CHECK (f.editorState.getSelectedStepRange().getLength() == f.stepsPerBeat());
}

TEST_CASE ("a mod-click on the ruler loops from the playhead to the click", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;
    f.engine.setPlayheadSteps (1.0);

    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (10.5), mod));

    const auto range = f.editorState.getSelectedStepRange();

    // Snapped outwards at both ends, so the span is never smaller than what
    // was asked for.
    CHECK (range.getStart() == 0);
    CHECK (range.getEnd() == 12);
}

TEST_CASE ("a mod-DRAG restarts the span at the press, rather than appending", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;
    f.engine.setPlayheadSteps (0.0);

    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (8.5), mod));

    // The press alone still means "from the playhead": that is the control
    // case, and without it this test would pass for the wrong reason.
    REQUIRE (f.editorState.getSelectedStepRange().getStart() == 0);

    f.roll.mouseDrag (eventAt (f.roll, f.rulerAt (10.5), mod, 1, true));

    const auto range = f.editorState.getSelectedStepRange();

    INFO ("after the drag: " << range.getStart() << ".." << range.getEnd());
    CHECK (range.getStart() == 8);
    CHECK (range.getEnd() == 12);
}

TEST_CASE ("a shift-click that never moves takes the span back", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;

    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (4.5), shift));
    REQUIRE (f.editorState.hasStepSelection());

    f.roll.mouseUp (eventAt (f.roll, f.rulerAt (4.5), shift));
    CHECK_FALSE (f.editorState.hasStepSelection());
}

TEST_CASE ("a mod-click that never moves keeps its span", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;
    f.engine.setPlayheadSteps (0.0);

    f.roll.mouseDown (eventAt (f.roll, f.rulerAt (10.5), mod));
    f.roll.mouseUp (eventAt (f.roll, f.rulerAt (10.5), mod));

    // A mod-click has already said what it wants; only a shift-click means
    // "nothing" when it does not move.
    CHECK (f.editorState.hasStepSelection());
}

TEST_CASE ("double-clicking the piano roll ruler clears the span", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    RollFixture f;
    f.editorState.setSelectedStepRange ({ 0, 8 });

    f.roll.mouseDoubleClick (eventAt (f.roll, f.rulerAt (6.0), juce::ModifierKeys(), 2));
    CHECK_FALSE (f.editorState.hasStepSelection());
}

TEST_CASE ("the sequencer ruler can select a span and clear it", "[ruler]")
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

    const auto y = strip->getHeight() / 2;
    const auto beat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);

    // This ruler could only ever seek: it painted the piano roll's span but had
    // no way to take one out, which is what made the sequencer the odd one.
    strip->mouseDown (eventAt (*strip, { strip->getWidth() / 4, y }, shift));
    strip->mouseDrag (eventAt (*strip, { strip->getWidth() / 2, y }, shift, 1, true));

    const auto range = editorState.getSelectedStepRange();

    INFO ("selected " << range.getStart() << ".." << range.getEnd());
    REQUIRE (editorState.hasStepSelection());
    CHECK (range.getStart() % beat == 0);
    CHECK (range.getEnd() % beat == 0);

    strip->mouseDoubleClick (eventAt (*strip, { strip->getWidth() / 2, y },
                                      juce::ModifierKeys(), 2));
    CHECK_FALSE (editorState.hasStepSelection());
}

TEST_CASE ("the playlist ruler selects whole bars, and double-click clears", "[ruler]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    EditorState editorState;
    PlaylistComponent playlist { document, engine, editorState };

    playlist.setSize (1200, 600);
    playlist.setVisible (true);
    playlist.resized();

    const auto strip = playlist.getRulerArea();
    const auto y = strip.getCentreY();
    const auto x = [&] (double bar)
    {
        return strip.getX() + (int) playlist.getTimeline().xForStep (bar);
    };

    playlist.mouseDown (eventAt (playlist, { x (1.5), y }, shift));
    playlist.mouseDrag (eventAt (playlist, { x (3.5), y }, shift, 1, true));

    const auto range = editorState.getSelectedBarRange();

    // Bars, because this timeline counts bars - the gesture never converts.
    INFO ("selected bars " << range.getStart() << ".." << range.getEnd());
    CHECK (range.getStart() == 1);
    CHECK (range.getEnd() == 4);

    playlist.mouseDoubleClick (eventAt (playlist, { x (2.5), y }, juce::ModifierKeys(), 2));
    CHECK_FALSE (editorState.hasBarSelection());
}
