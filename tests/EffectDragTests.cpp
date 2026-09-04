// Reordering a chain by dragging a card's grip.
//
// Split out of an EffectTests.cpp that was 1,746 lines, along the Catch2
// tags it already carried.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ModuleCatalog.h"
#include "ui/design/Gestures.h"

#include "EffectChainHarness.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("a twitch on the grip reorders nothing", "[effects][ui][drag]")
{
    // The drag committed on the first pixel that crossed into a neighbouring
    // card, with no threshold at all, so a hand that was not quite still while
    // pressing a grip reordered the chain.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    const auto before = h.typesInOrder();
    h.document.getUndoManager().clearUndoHistory();

    const auto start = h.centreOf (0);

    h.chain.beginReorder (0, start);
    h.chain.updateReorder (start.translated (0, gesture::dragThresholdPx - 1));

    CHECK_FALSE (h.chain.isReordering());
    CHECK (h.chain.getReorderInsertion() == -1);
    CHECK (h.chain.getDropArea().isEmpty());

    h.chain.endReorder (true);

    CHECK (h.typesInOrder() == before);
    CHECK_FALSE (h.document.getUndoManager().canUndo());
}

TEST_CASE ("a dragged card leaves its place and the chain opens a gap", "[effects][ui][drag]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (const auto orientation : { EffectChainComponent::Orientation::vertical,
                                    EffectChainComponent::Orientation::horizontal })
    {
        ChainHarness h { orientation };

        h.chain.addEffectOfType ("filter");
        h.chain.addEffectOfType ("delay");
        h.chain.addEffectOfType ("reverb");
        h.layOutLikeAHost();

        const auto resting = h.cardBounds();

        h.dragFrom (0, h.pastTheEnd());

        INFO ((h.chain.isHorizontal() ? "horizontal" : "vertical"));

        REQUIRE (h.chain.isReordering());

        // A real hole in the row, the size of the card that came out of it.
        const auto gap = h.chain.getDropArea();
        REQUIRE_FALSE (gap.isEmpty());

        if (h.chain.isHorizontal())
            CHECK (gap.getWidth() == resting[0].getWidth());
        else
            CHECK (gap.getHeight() == resting[0].getHeight());

        // The card itself is somewhere else - under the cursor.
        CHECK (h.chain.getSlotBounds (0) != resting[0]);

        // And the others have moved up to close ranks behind it.
        CHECK (h.chain.getSlotBounds (1) != resting[1]);

        h.chain.endReorder (false);

        // Cancelled, everything back where it was.
        CHECK (h.cardBounds() == resting);
        CHECK_FALSE (h.chain.isReordering());
    }
}

TEST_CASE ("a whole reorder drag is one undo step", "[effects][ui][drag]")
{
    // Two defects in one. The drag committed every time the cursor crossed a
    // card boundary, each in its OWN transaction, so undoing a drag across two
    // slots took two undos - and each commit rebuilt the cards, which deleted
    // the card whose mouseDrag was on the stack and ended the gesture after a
    // single slot.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (const auto orientation : { EffectChainComponent::Orientation::vertical,
                                    EffectChainComponent::Orientation::horizontal })
    {
        ChainHarness h { orientation };

        h.chain.addEffectOfType ("filter");
        h.chain.addEffectOfType ("delay");
        h.chain.addEffectOfType ("reverb");
        h.layOutLikeAHost();

        const juce::StringArray before { "filter", "delay", "reverb" };
        REQUIRE (h.typesInOrder() == before);

        h.document.getUndoManager().clearUndoHistory();

        // All the way past the end, which is two slots - not one.
        h.dragFrom (0, h.pastTheEnd());
        h.chain.endReorder (true);

        INFO ((h.chain.isHorizontal() ? "horizontal" : "vertical"));
        REQUIRE (h.typesInOrder() == juce::StringArray { "delay", "reverb", "filter" });

        juce::UndoManager& undo = h.document.getUndoManager();
        CHECK (undo.getUndoDescription() == "Reorder effects");

        // ONE undo, not two.
        REQUIRE (undo.undo());
        CHECK (h.typesInOrder() == before);
        CHECK_FALSE (undo.canUndo());
    }
}

TEST_CASE ("where a card lands does not depend on how it got there", "[effects][ui][drag]")
{
    // The insertion point is a hit test against where the cards were when the
    // drag STARTED. Against live bounds it would be reading a layout the drag
    // itself had just changed - the gap moves every card after it - and the
    // drop point would flip back and forth across a boundary the pointer was
    // not crossing.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.chain.addEffectOfType ("eq");
    h.layOutLikeAHost();

    const auto span = h.chain.getRequiredHeight();
    const auto samples = 12;

    juce::Array<int> descending, ascending;

    // The WHOLE gesture, sampled frame by frame, rather than its two ends.
    const auto start = h.centreOf (1);
    h.chain.beginReorder (1, start);

    for (int i = 0; i <= samples; ++i)
    {
        h.chain.updateReorder ({ start.x, span * i / samples });
        descending.add (h.chain.getReorderInsertion());
    }

    h.chain.endReorder (false);

    h.chain.beginReorder (1, start);

    for (int i = samples; i >= 0; --i)
    {
        h.chain.updateReorder ({ start.x, span * i / samples });
        ascending.insert (0, h.chain.getReorderInsertion());
    }

    h.chain.endReorder (false);

    INFO ("down: " << juce::String (descending.size())
                   << " up: " << juce::String (ascending.size()));
    CHECK (descending == ascending);

    // And it really did move over the drag rather than sitting still.
    CHECK (descending.getFirst() != descending.getLast());

    // Monotonic: further along the chain is never an earlier slot.
    for (int i = 1; i < descending.size(); ++i)
    {
        INFO ("sample " << i);
        CHECK (descending[i] >= descending[i - 1]);
    }
}

TEST_CASE ("escape abandons a reorder", "[effects][ui][drag]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    const juce::StringArray before { "filter", "delay", "reverb" };
    h.document.getUndoManager().clearUndoHistory();

    h.dragFrom (0, h.pastTheEnd());
    REQUIRE (h.chain.isReordering());
    REQUIRE (h.chain.getReorderInsertion() == 2);

    REQUIRE (h.chain.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));

    CHECK_FALSE (h.chain.isReordering());
    CHECK (h.typesInOrder() == before);
    CHECK_FALSE (h.document.getUndoManager().canUndo());

    // And with nothing being dragged, escape is not the chain's key to take.
    CHECK_FALSE (h.chain.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
}

TEST_CASE ("letting go outside the chain moves nothing", "[effects][ui][drag]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    const juce::StringArray before { "filter", "delay", "reverb" };
    h.document.getUndoManager().clearUndoHistory();

    // Well clear of the chain: dropped on whatever is above it, which is not a
    // reorder of anything.
    h.dragFrom (0, { -400, -400 });

    REQUIRE (h.chain.isReordering());
    CHECK (h.chain.getReorderInsertion() == 0);

    h.chain.endReorder (true);

    CHECK (h.typesInOrder() == before);
    CHECK_FALSE (h.document.getUndoManager().canUndo());
}

TEST_CASE ("a card can be dropped after the last one", "[effects][ui][drag]")
{
    // The old hit test answered with a SLOT, so the furthest it could say was
    // "on the last card". A drop needs a gap, and there is one more gap than
    // there are cards.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (const auto orientation : { EffectChainComponent::Orientation::vertical,
                                    EffectChainComponent::Orientation::horizontal })
    {
        ChainHarness h { orientation };

        h.chain.addEffectOfType ("filter");
        h.chain.addEffectOfType ("delay");
        h.chain.addEffectOfType ("reverb");
        h.layOutLikeAHost();

        INFO ((h.chain.isHorizontal() ? "horizontal" : "vertical"));

        CHECK (h.insertionAfterDragging (0, h.pastTheEnd()) == 2);
        CHECK (h.insertionAfterDragging (2, h.beforeTheStart()) == 0);

        // The middle card, dropped past the end, lands last.
        h.dragFrom (1, h.pastTheEnd());
        h.chain.endReorder (true);

        CHECK (h.typesInOrder() == juce::StringArray { "filter", "reverb", "delay" });
    }
}

TEST_CASE ("the drop area is drawn, not merely worked out", "[effects][ui][drag]")
{
    // A gap the chain knows about and never paints is exactly the state this
    // change exists to end: the reorder always "worked", it just never showed
    // you anything. So the assertion is on the pixels.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    // Taller than the cards need, so there is somewhere in the chain that
    // nothing paints - which is the control below.
    h.chain.setSize (h.chain.getWidth(), h.chain.getRequiredHeight() * 4);

    h.dragFrom (0, h.pastTheEnd());

    const auto gap = h.chain.getDropArea();
    REQUIRE_FALSE (gap.isEmpty());

    const auto image = dew::testing::render (h.chain);

    // Fraction of a region the chain actually put something in. The image
    // starts transparent and the chain fills only what it draws, so this tells
    // "painted" from "not painted" with no colour to argue about - and the
    // knobs are drawn in the accent colour, so counting accent pixels would
    // have measured the controls rather than the marker.
    const auto painted = [&image] (juce::Rectangle<int> area)
    {
        const auto clipped = image.getClippedImage (area);
        auto opaque = 0;

        for (int y = 0; y < clipped.getHeight(); ++y)
            for (int x = 0; x < clipped.getWidth(); ++x)
                if (clipped.getPixelAt (x, y).getAlpha() > 0)
                    ++opaque;

        return opaque;
    };

    // No card is in the gap - they have all moved out of it - so anything there
    // is the mark the chain drew to say where the card will land.
    CHECK (painted (gap) > gap.getWidth() * gap.getHeight() / 2);

    // THE CONTROL. A strip of the chain that neither a card nor the gap covers
    // has to come back empty, or the measurement above is finding the whole
    // component and would pass with nothing drawn at all.
    juce::Rectangle<int> blank;

    for (int y = 0; y + gap.getHeight() <= h.chain.getHeight(); ++y)
    {
        const juce::Rectangle<int> candidate { 0, y, h.chain.getWidth(), gap.getHeight() };
        auto clear = ! candidate.intersects (gap);

        for (int i = 0; clear && i < h.chain.getNumSlotRows(); ++i)
            clear = ! candidate.intersects (h.chain.getSlotBounds (i));

        if (clear)
        {
            blank = candidate;
            break;
        }
    }

    REQUIRE_FALSE (blank.isEmpty());
    CHECK (painted (blank) == 0);
}

TEST_CASE ("the cards part rather than jumping", "[effects][ui][drag][motion]")
{
    // Motion is off unless the application turns it on, so every other test
    // here sees the final layout immediately. This one drives the clock by
    // hand and walks the whole parting frame by frame.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ScopedAnimation animating;

    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    // A rebuild ARRIVES rather than sweeping: a chain built from a document
    // must not slide every card down from the top of the panel.
    const auto resting = h.cardBounds();
    Animator::shared().advance (tokens::motion::quickMs);
    REQUIRE (h.cardBounds() == resting);

    h.dragFrom (0, h.pastTheEnd());

    const auto settled = juce::Rectangle<int> { 0, 0, h.chain.getWidth(),
                                                h.chain.getSlotBounds (1).getHeight() };

    // Card 1 has to travel to the top of the chain. Part way through the ease
    // it is neither where it was nor where it is going.
    Animator::shared().advance (tokens::motion::quickMs / 3);

    const auto midway = h.chain.getSlotBounds (1);
    CHECK (midway.getY() < resting[1].getY());
    CHECK (midway.getY() > settled.getY());

    // And it gets there.
    Animator::shared().advance (tokens::motion::quickMs);
    CHECK (h.chain.getSlotBounds (1).getY() == settled.getY());
}
