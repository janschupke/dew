// =============================================================================
// The reorder drag.
//
// The same class, a second translation unit beside EffectChainComponent.cpp.
//
// The gesture lives on the CHAIN rather than on the card, and that is the whole
// point of it. Committing a move rebuilds the chain, so a card that owned its
// own drag was deleted in the middle of its own mouseDrag, and the line after
// the commit then wrote a flag into freed memory.
//
// Two rules hold across all of it. Every hit test runs against `frozen` - where
// the cards were when the drag STARTED - because hit-testing live bounds while
// the cards are being moved is a feedback loop. And a commit is ONE undo
// transaction for the whole drag, however many cards the pointer crossed.
// =============================================================================

#include "ui/EffectChainComponent.h"

#include "ui/EffectCard.h"

#include "model/ProjectEdits.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

namespace
{
/** How far past the chain a dragged card may go and still be a drop.

    A little slack, because letting go a pixel outside the edge of a narrow
    column is an ordinary thing to do and losing the whole drag for it is not.
*/
constexpr int dropMargin = space::xl;

} // namespace

int EffectChainComponent::insertionFor (juce::Point<int> position) const
{
    // Off the end of the chain is not a drop. The insertion point goes back to
    // where the card came from, so letting go out there moves nothing - which
    // is the only sensible reading of a card dropped on the mixer strips above.
    if (! getLocalBounds().expanded (dropMargin).contains (position))
        return reorder.source;

    const auto horizontal = isHorizontal();
    const auto along = horizontal ? position.x : position.y;

    // How many of the OTHER cards the pointer is past the middle of. A midpoint
    // rather than an edge, because an edge means a card has to be dragged
    // clear of its neighbour before anything happens - and against the FROZEN
    // bounds, so this is a pure function of where the pointer is and not of how
    // it got there.
    auto before = 0;

    for (int i = 0; i < reorder.frozen.size(); ++i)
    {
        if (i == reorder.source)
            continue;

        const auto& bounds = reorder.frozen.getReference (i);

        if (along > (horizontal ? bounds.getCentreX() : bounds.getCentreY()))
            ++before;
    }

    return before;
}

void EffectChainComponent::layOutCards()
{
    dropArea = {};

    if (cards.isEmpty())
        return;

    // The order the cards are shown in. During a drag that is the order they
    // WILL be in when it is let go, so what you are looking at is the result
    // rather than a preview of one.
    juce::Array<int> order;

    for (int i = 0; i < cards.size(); ++i)
        if (i != reorder.source)
            order.add (i);

    if (reorder.source >= 0)
        order.insert (juce::jlimit (0, order.size(), reorder.insertAt), reorder.source);

    const auto horizontal = isHorizontal();
    auto along = horizontal ? space::xs : 0;

    for (const auto i : order)
    {
        auto* card = cards[i];
        const auto extent = horizontal ? card->getRequiredWidth() : card->getRequiredHeight();

        if (i == reorder.source && reorder.active)
            dropArea = horizontal ? juce::Rectangle<int> (along, 0, extent, EffectCard::cardHeight)
                                  : juce::Rectangle<int> (0, along, getWidth(), extent);
        else if (snapNextLayout)
            slide[i]->snapTo ((float) along);
        else
            slide[i]->animateTo ((float) along, motion::quickMs);

        along += extent + space::xs;
    }

    // The FIRST position a card is given arrives, it does not sweep. Without
    // this a chain built from a document would slide every card down from the
    // top of the panel the moment the application turned motion on.
    snapNextLayout = false;

    applyCardPositions();
}

void EffectChainComponent::applyCardPositions()
{
    const auto horizontal = isHorizontal();

    for (int i = 0; i < cards.size(); ++i)
    {
        auto* card = cards[i];

        if (i == reorder.source && reorder.active)
            continue; // it is under the cursor, not in the row

        const auto along = juce::roundToInt (slide[i]->get());

        if (horizontal)
            card->setBounds (along, 0, card->getRequiredWidth(), EffectCard::cardHeight);
        else
            card->setBounds (0, along, getWidth(), card->getRequiredHeight());
    }

    if (! reorder.active)
        return;

    // One clone under the cursor, and it is the live card rather than a picture
    // of one: its knobs keep drawing, and a test can read where it got to.
    auto* dragged = cards[reorder.source];
    const auto extent = horizontal ? dragged->getRequiredWidth() : dragged->getRequiredHeight();
    const auto wanted = (horizontal ? reorder.cursor.x - reorder.grabOffset.x
                                    : reorder.cursor.y - reorder.grabOffset.y);
    const auto limit = juce::jmax (0, (horizontal ? getWidth() : getHeight()) - extent);
    const auto placed = juce::jlimit (0, limit, wanted);

    if (horizontal)
        dragged->setBounds (placed, 0, extent, EffectCard::cardHeight);
    else
        dragged->setBounds (0, placed, getWidth(), extent);
}

void EffectChainComponent::beginReorder (int slot, juce::Point<int> position)
{
    if (! juce::isPositiveAndBelow (slot, cards.size()))
        return;

    reorder = {};
    reorder.source = slot;
    reorder.insertAt = slot;
    reorder.pressedAt = position;
    reorder.cursor = position;
    reorder.grabOffset = position - cards[slot]->getPosition();

    for (auto* card : cards)
        reorder.frozen.add (card->getBounds());
}

void EffectChainComponent::updateReorder (juce::Point<int> position)
{
    if (reorder.source < 0)
        return;

    reorder.cursor = position;

    if (! reorder.active)
    {
        if (! gesture::passedThreshold (reorder.pressedAt, position))
            return;

        reorder.active = true;

        auto* dragged = cards[reorder.source];
        dragged->setAlpha (emphasis::dimmed);
        dragged->toFront (false);

        // So escape can abandon the drag. Asked for here rather than held all
        // the time: the chain has nothing else to do with the keyboard.
        grabKeyboardFocus();
    }

    const auto wanted = insertionFor (position);

    if (wanted != reorder.insertAt)
        reorder.insertAt = wanted;

    layOutCards();
    repaint();

    if (onDragNearEdge != nullptr)
        onDragNearEdge (position);
}

void EffectChainComponent::endReorder (bool commit)
{
    // Everything this needs, read out before anything can rebuild the cards.
    const auto source = reorder.source;
    const auto target = reorder.insertAt;
    const auto moved = reorder.active && commit && target != source;

    if (juce::isPositiveAndBelow (source, cards.size()))
        cards[source]->setAlpha (1.0f);

    reorder = {};

    if (! moved)
    {
        // Cancelled, or let go where it started. Nothing reaches the document,
        // so there is no undo entry for a drag that changed nothing.
        layOutCards();
        repaint();
        return;
    }

    auto effect = effectAt (source);

    if (! effect.isValid())
        return;

    // ONE transaction for the whole drag. It used to open a new one at every
    // card boundary the pointer crossed, so undoing a drag across three slots
    // took three undos - and each of those rebuilt the cards mid-gesture.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Reorder effects");
    ProjectEdits::moveEffect (chainOwner, effect, target, &undo);
    selectedSlot = target;

    // rebuild() arrives from valueTreeChildOrderChanged, and deletes the card
    // whose mouseUp called this. Nothing below may touch it.
}
} // namespace dew
