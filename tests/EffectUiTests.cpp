// The chain editor: what it shows, and how it lays out either way round.
//
// Split out of an EffectTests.cpp that was 1,746 lines, along the Catch2
// tags it already carried.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ModuleCatalog.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/EffectCard.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

#include "EffectChainHarness.h"
#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("the chain editor follows the chain it is pointed at", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    REQUIRE (h.chain.getNumSlotRows() == 0);

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == 1);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb" });

    h.chain.addEffectOfType ("delay");
    REQUIRE (h.chain.getNumSlotRows() == 2);

    // An edit made anywhere else still reaches the editor.
    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addEffect (h.document.getState(), h.channel(), "drive", &undo);
    REQUIRE (h.chain.getNumSlotRows() == 3);

    ProjectEdits::removeEffect (h.channel(), h.channel().getChildWithName (ids::EFFECT), &undo);
    REQUIRE (h.chain.getNumSlotRows() == 2);
}

TEST_CASE ("pointing the editor at another chain shows that chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    juce::UndoManager& undo = h.document.getUndoManager();

    auto mixerTrack = h.document.getState()
                          .getChildWithName (ids::MIXER)
                          .getChildWithName (ids::MIXER_TRACK);
    REQUIRE (mixerTrack.isValid());

    h.chain.addEffectOfType ("reverb");
    ProjectEdits::addEffect (h.document.getState(), mixerTrack, "eq", &undo);
    ProjectEdits::addEffect (h.document.getState(), mixerTrack, "drive", &undo);

    REQUIRE (h.chain.getNumSlotRows() == 1);

    h.chain.setOwner (mixerTrack);
    REQUIRE (h.chain.getNumSlotRows() == 2);
    REQUIRE (h.chain.getSelectedSlot() == 0);

    // And back, without carrying the other chain's selection into it.
    h.chain.setOwner (h.channel());
    REQUIRE (h.chain.getNumSlotRows() == 1);
    REQUIRE (h.chain.getSelectedSlot() == 0);
}

TEST_CASE ("the editor never selects a slot that is not there", "[effects][ui]")
{
    // Deleting the selected slot used to be the obvious way to leave the
    // parameter area pointing at a removed effect.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    h.chain.selectSlot (2);
    REQUIRE (h.chain.getSelectedSlot() == 2);

    juce::UndoManager& undo = h.document.getUndoManager();
    juce::Array<juce::ValueTree> effects;

    for (const auto& child : h.channel())
        if (child.hasType (ids::EFFECT))
            effects.add (child);

    ProjectEdits::removeEffect (h.channel(), effects.getLast(), &undo);

    REQUIRE (h.chain.getNumSlotRows() == 2);
    REQUIRE (h.chain.getSelectedSlot() < h.chain.getNumSlotRows());

    // Asking for a slot beyond the end clamps rather than going out of range.
    h.chain.selectSlot (99);
    REQUIRE (h.chain.getSelectedSlot() == 1);

    h.chain.selectSlot (-5);
    REQUIRE (h.chain.getSelectedSlot() == 0);
}

TEST_CASE ("the add button stops at a full chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    for (int i = 0; i < kMaxEffectsPerChain; ++i)
        h.chain.addEffectOfType ("filter");

    REQUIRE (h.chain.getNumSlotRows() == kMaxEffectsPerChain);

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == kMaxEffectsPerChain);
    REQUIRE (h.typesInOrder().size() == kMaxEffectsPerChain);
}

TEST_CASE ("an editor pointed at nothing is empty rather than stale", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("reverb");
    REQUIRE (h.chain.getNumSlotRows() == 1);

    h.chain.setOwner ({});
    REQUIRE (h.chain.getNumSlotRows() == 0);

    // And adding into nothing does not throw or write anywhere.
    h.chain.addEffectOfType ("delay");
    REQUIRE (h.chain.getNumSlotRows() == 0);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb" });
}

TEST_CASE ("cards expand and collapse independently", "[effects][ui]")
{
    // The old editor showed one slot's parameters at a time, so comparing a
    // filter's cutoff against a delay's time meant clicking between them.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    REQUIRE (h.chain.getNumSlotRows() == 3);

    // Adding an effect opens it: you added it to set it up.
    REQUIRE (h.chain.isSlotExpanded (2));

    h.chain.setSlotExpanded (0, true);
    h.chain.setSlotExpanded (1, true);

    REQUIRE (h.chain.isSlotExpanded (0));
    REQUIRE (h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (2));

    const auto allOpen = h.chain.getRequiredHeight();

    h.chain.setSlotExpanded (1, false);
    REQUIRE (! h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (0));
    REQUIRE (h.chain.isSlotExpanded (2));

    // Closing one makes the chain shorter, which is what the host scrolls.
    REQUIRE (h.chain.getRequiredHeight() < allOpen);
}

namespace
{

/** The card in slot `slot`.

    The cards are the chain's children, but not in slot ORDER: one being dragged
    is brought to the front, so the child list is paint order. getSlotBounds is
    the seam that already answers "where is slot n", so matching on it needs no
    new API.
*/
EffectCard* cardAt (EffectChainComponent& chain, int slot)
{
    const auto bounds = chain.getSlotBounds (slot);

    for (auto* child : chain.getChildren())
        if (auto* card = dynamic_cast<EffectCard*> (child))
            if (card->getBounds() == bounds)
                return card;

    return nullptr;
}

/** A press and a release on the card, as if they had arrived from `origin`.

    forwardChildMouseEventsTo makes the card a listener on every control inside
    it, so this is exactly the shape of event a button in the header delivers -
    eventComponent is the card, originalComponent is the button.
*/
void pressAndRelease (EffectCard& card, juce::Component& origin, juce::Point<int> where)
{
    const auto position = where.toFloat();

    const auto event = [&]
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                                 juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &card, &origin,
                                 juce::Time::getCurrentTime(), position,
                                 juce::Time::getCurrentTime(), 1, false);
    };

    card.mouseDown (event());
    card.mouseUp (event());
}

} // namespace

TEST_CASE ("a header button folds the card once, and the others not at all", "[effects][ui]")
{
    // The defect: EffectCard's constructor calls forwardChildMouseEventsTo, so
    // a release on the chevron arrived TWICE - once as the button's own click,
    // once here as a press on the header - and the two toggles cancelled. The
    // chevron did nothing at all, which is what "not collapsing reliably" was,
    // and bypass, preset, up, down and remove each folded the card as a silent
    // side effect of being pressed.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.layOutLikeAHost();

    auto* card = cardAt (h.chain, 0);
    REQUIRE (card != nullptr);

    auto* chevron = card->findChildWithID ("effectExpand");
    REQUIRE (chevron != nullptr);

    const auto wasOpen = h.chain.isSlotExpanded (0);

    // The forwarded half on its own changes nothing: the release belongs to the
    // button, and the button has its own answer to it.
    pressAndRelease (*card, *chevron, chevron->getBounds().getCentre());
    CHECK (h.chain.isSlotExpanded (0) == wasOpen);

    // Both halves, which is what a real click is. Exactly one toggle.
    pressAndRelease (*card, *chevron, chevron->getBounds().getCentre());
    dynamic_cast<juce::Button&> (*chevron).onClick();
    CHECK (h.chain.isSlotExpanded (0) == ! wasOpen);

    // And a press on any OTHER header button leaves the fold alone.
    for (auto* child : card->getChildren())
    {
        if (child == chevron || dynamic_cast<juce::Button*> (child) == nullptr)
            continue;

        pressAndRelease (*card, *child, child->getBounds().getCentre());
        CHECK (h.chain.isSlotExpanded (0) == ! wasOpen);
    }
}

TEST_CASE ("a click on the header itself still folds the card", "[effects][ui]")
{
    // The other half of the fix: the header is still the large target, and a
    // press that lands on it rather than on one of its buttons must work as it
    // always did.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.layOutLikeAHost();

    auto* card = cardAt (h.chain, 0);
    REQUIRE (card != nullptr);

    const auto wasOpen = h.chain.isSlotExpanded (0);

    // The middle of the header row, which is the name - no button is there.
    pressAndRelease (*card, *card, { card->getWidth() / 2, tokens::size::rowHeight / 2 });

    CHECK (h.chain.isSlotExpanded (0) == ! wasOpen);
}

TEST_CASE ("which cards are open survives a rebuild, and follows the effect", "[effects][ui]")
{
    // Expansion is keyed on the effect id, not its position, so reordering a
    // chain does not shuffle which cards are open.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    h.chain.setSlotExpanded (0, true);
    h.chain.setSlotExpanded (1, false);
    h.chain.setSlotExpanded (2, false);

    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });
    REQUIRE (h.chain.isSlotExpanded (0));

    // Move the open filter to the end.
    h.chain.moveSlot (0, 2);

    REQUIRE (h.typesInOrder() == juce::StringArray { "delay", "reverb", "filter" });
    REQUIRE (! h.chain.isSlotExpanded (0));
    REQUIRE (! h.chain.isSlotExpanded (1));
    REQUIRE (h.chain.isSlotExpanded (2)); // still the filter

    // And an unrelated document change does not close anything.
    juce::UndoManager& undo = h.document.getUndoManager();
    ProjectEdits::addChannel (h.document.getState(), "Extra", &undo);
    REQUIRE (h.chain.isSlotExpanded (2));
}

TEST_CASE ("expansion is view state, not document state", "[effects][ui]")
{
    // Opening a card must not put anything on the undo stack or make the
    // project dirty - it is not a change to the music.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");

    const auto before = ProjectSerializer::toJsonString (h.document.getState());

    h.document.getUndoManager().clearUndoHistory();
    h.chain.setSlotExpanded (0, false);
    h.chain.setSlotExpanded (0, true);

    REQUIRE (! h.document.getUndoManager().canUndo());
    REQUIRE (ProjectSerializer::toJsonString (h.document.getState()) == before);
}

TEST_CASE ("dragging a card by its grip reorders the chain", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");

    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });

    // moveSlot is what the grip drives, and what the up/down buttons drive too.
    h.chain.moveSlot (2, 0);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb", "filter", "delay" });

    // Out-of-range targets clamp rather than dropping the effect.
    h.chain.moveSlot (0, 99);
    REQUIRE (h.typesInOrder() == juce::StringArray { "filter", "delay", "reverb" });

    h.chain.moveSlot (2, -5);
    REQUIRE (h.typesInOrder() == juce::StringArray { "reverb", "filter", "delay" });
}

TEST_CASE ("the grip drops a card where the cursor is", "[effects][ui]")
{
    // The reorder used to divide how far the cursor had travelled by a row
    // height - and by the FOLDED row height, though an open card is three times
    // that tall, so dragging one card down by its own height moved it three
    // places. A row's cards are not all the same width either, so there is no
    // divisor that would work. It is a hit test now.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (const auto orientation : { EffectChainComponent::Orientation::vertical,
                                    EffectChainComponent::Orientation::horizontal })
    {
        ChainHarness h { orientation };

        h.chain.addEffectOfType ("filter");
        h.chain.addEffectOfType ("delay");
        h.chain.addEffectOfType ("reverb");
        h.layOutLikeAHost();

        const auto cards = h.cardBounds();
        REQUIRE (cards.size() == 3);

        for (int i = 0; i < cards.size(); ++i)
        {
            INFO ("card " << i << " at " << cards[i].toString());
            REQUIRE (h.chain.slotAtPosition (cards[i].getCentre()) == i);
        }

        // Dragged past the end it lands on the last card, not out of range.
        REQUIRE (h.chain.slotAtPosition (cards.getLast().getBottomRight()
                                         + juce::Point<int> { 400, 400 })
                 == 2);
    }
}

TEST_CASE ("a chain in a row lays its cards side by side", "[effects][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h { EffectChainComponent::Orientation::horizontal };

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.chain.addEffectOfType ("eq");
    h.layOutLikeAHost();

    const auto cards = h.cardBounds();
    REQUIRE (cards.size() == 4);

    for (int i = 0; i < cards.size(); ++i)
    {
        INFO ("card " << i << " at " << cards[i].toString());

        // Every card has real width. removeFromLeft on a fixed rectangle clamps
        // at the right edge, which is how a mixer full of strips used to give
        // the last of them nothing at all - the chain asks for the width it
        // needs and the host scrolls it instead.
        REQUIRE (cards[i].getWidth() > 0);
        REQUIRE (cards[i].getY() == cards[0].getY());
        REQUIRE (cards[i].getHeight() == cards[0].getHeight());

        if (i > 0)
            REQUIRE (cards[i].getX() >= cards[i - 1].getRight());
    }

    REQUIRE (cards.getLast().getRight() <= h.chain.getRequiredWidth());
}

TEST_CASE ("a row of cards is one card tall however many effects it holds", "[effects][ui]")
{
    // The mixer sizes its effect row from this. A row that grew as you filled
    // it would shove the faders about every time you added a delay.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h { EffectChainComponent::Orientation::horizontal };

    const auto rowHeight = h.chain.getRequiredHeight();
    auto width = h.chain.getRequiredWidth();

    for (const auto* type : { "filter", "delay", "reverb", "eq" })
    {
        h.chain.addEffectOfType (type);
        h.layOutLikeAHost();

        INFO ("after adding " << type);
        REQUIRE (h.chain.getRequiredHeight() == rowHeight);
        REQUIRE (h.chain.getRequiredWidth() > width);

        width = h.chain.getRequiredWidth();
    }

    // Folding is hidden in a row, and asking for it changes nothing.
    h.chain.setSlotExpanded (0, false);
    h.layOutLikeAHost();

    REQUIRE (h.chain.getRequiredHeight() == rowHeight);
    REQUIRE (h.cardBounds()[0].getHeight() == h.cardBounds()[1].getHeight());
}

TEST_CASE ("a chain in a column still stacks its cards", "[effects][ui]")
{
    // The instrument panel's chain. Nothing about the mixer's row reaches it,
    // which is why vertical is the default rather than a mode you switch to.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ChainHarness h;

    REQUIRE (! h.chain.isHorizontal());

    h.chain.addEffectOfType ("filter");
    h.chain.addEffectOfType ("delay");
    h.chain.addEffectOfType ("reverb");
    h.layOutLikeAHost();

    const auto cards = h.cardBounds();
    REQUIRE (cards.size() == 3);

    for (int i = 0; i < cards.size(); ++i)
    {
        INFO ("card " << i << " at " << cards[i].toString());

        REQUIRE (cards[i].getHeight() > 0);
        REQUIRE (cards[i].getX() == cards[0].getX());
        REQUIRE (cards[i].getWidth() == cards[0].getWidth());

        if (i > 0)
            REQUIRE (cards[i].getY() >= cards[i - 1].getBottom());
    }

    // And a card still folds, which is the whole reason the column exists.
    h.chain.setSlotExpanded (0, true);
    h.chain.setSlotExpanded (1, false);
    h.layOutLikeAHost();

    const auto folded = h.cardBounds();
    REQUIRE (folded[0].getHeight() > folded[1].getHeight());
}

TEST_CASE ("a frequency field drags by ratio, not by hertz", "[effects][ui]")
{
    // A cutoff over 20 to 18000 Hz dragged linearly gives about 70 Hz per
    // pixel, so the whole musically useful low end is the first three pixels.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    DewNumberField linear, logarithmic;

    for (auto* field : { &linear, &logarithmic })
    {
        field->setRange (20.0, 18000.0, 1.0);
        field->setValue (1000.0, juce::dontSendNotification);
        field->setSize (90, 40);
    }

    logarithmic.setLogarithmic (true);

    const auto dragBy = [] (DewNumberField& field, int pixels)
    {
        const juce::Point<float> start { 45.0f, 20.0f };
        const auto end = start.translated (0.0f, (float) -pixels);

        const auto make = [&field] (juce::Point<float> position, juce::Point<float> down)
        {
            return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(),
                                      position,
                                      juce::ModifierKeys(),
                                      1.0f,
                                      0.0f,
                                      0.0f,
                                      0.0f,
                                      0.0f,
                                      &field,
                                      &field,
                                      juce::Time::getCurrentTime(),
                                      down,
                                      juce::Time::getCurrentTime(),
                                      1,
                                      false };
        };

        field.mouseDown (make (start, start));
        field.mouseDrag (make (end, start));
        field.mouseUp (make (end, start));
    };

    dragBy (linear, 10);
    dragBy (logarithmic, 10);

    INFO ("linear " << linear.getValue() << " logarithmic " << logarithmic.getValue());

    // Ten pixels up is a huge jump linearly and a musical interval on a log
    // taper - which is the whole point.
    REQUIRE (linear.getValue() > 1600.0);
    REQUIRE (logarithmic.getValue() < 1400.0);
    REQUIRE (logarithmic.getValue() > 1000.0);
}
