#include "ui/EffectChainComponent.h"

#include "ui/EffectCard.h"

#include "model/PresetLibrary.h"

#include "engine/Effects.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
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

/** One effect: a header that is always visible, and a body that folds away. */

// -----------------------------------------------------------------------------

EffectChainComponent::EffectChainComponent (ProjectDocument& d, EditorState& s)
    : document (d)
    , editorState (s)
{
    setComponentID ("effectChain");

    // Only so escape can abandon a reorder; the chain grabs it when a drag
    // starts and never asks for it otherwise.
    setWantsKeyboardFocus (true);

    document.getState().addListener (this);
    editorState.addChangeListener (this);
}

EffectChainComponent::~EffectChainComponent()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void EffectChainComponent::setOwner (juce::ValueTree owner)
{
    if (chainOwner == owner)
        return;

    chainOwner = std::move (owner);
    selectedSlot = 0;
    rebuild();
}

void EffectChainComponent::setOrientation (Orientation wanted)
{
    if (std::exchange (orientation, wanted) == wanted)
        return;

    for (auto* card : cards)
        card->resized();

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

bool EffectChainComponent::canAddEffect() const
{
    return chainOwner.isValid() && cards.size() < kMaxEffectsPerChain;
}

juce::ValueTree EffectChainComponent::effectAt (int index) const
{
    int i = 0;

    for (const auto& child : chainOwner)
        if (child.hasType (ids::EFFECT) && i++ == index)
            return child;

    return {};
}

void EffectChainComponent::selectSlot (int index)
{
    const auto count = ProjectEdits::countEffects (chainOwner);
    const auto wanted = juce::jlimit (0, juce::jmax (0, count - 1), index);

    if (wanted == selectedSlot)
        return;

    selectedSlot = wanted;

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setSelected (i == selectedSlot);
}

bool EffectChainComponent::isSlotExpanded (int index) const
{
    const auto effect = effectAt (index);
    return effect.isValid() && editorState.isEffectExpanded ((int) effect[ids::id]);
}

void EffectChainComponent::setSlotExpanded (int index, bool expanded)
{
    if (const auto effect = effectAt (index); effect.isValid())
        editorState.setEffectExpanded ((int) effect[ids::id], expanded);
}

void EffectChainComponent::moveSlot (int from, int to)
{
    const auto count = ProjectEdits::countEffects (chainOwner);
    const auto target = juce::jlimit (0, juce::jmax (0, count - 1), to);

    if (from == target)
        return;

    auto effect = effectAt (from);

    if (! effect.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Reorder effects");
    ProjectEdits::moveEffect (chainOwner, effect, target, &undo);
    selectedSlot = target;
}

void EffectChainComponent::addEffectOfType (const juce::String& type)
{
    if (! chainOwner.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add effect");

    const auto added = ProjectEdits::addEffect (document.getState(), chainOwner, type, &undo);

    if (! added.isValid())
        return;

    selectedSlot = ProjectEdits::countEffects (chainOwner) - 1;

    // A new effect opens: you added it to set it up.
    editorState.setEffectExpanded ((int) added[ids::id], true);
}

void EffectChainComponent::showAddMenu (juce::Component& target)
{
    if (ProjectEdits::countEffects (chainOwner) >= kMaxEffectsPerChain)
        return;

    juce::PopupMenu menu;

    // Built from the catalog, so a new effect type appears in the picker
    // because it exists, not because someone remembered a third list.
    const auto& all = effectDescriptors();

    for (int i = 0; i < (int) all.size(); ++i)
        menu.addItem (i + 1, all[(size_t) i].displayName);

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [this] (int choice)
                        {
                            const auto& types = effectDescriptors();

                            if (choice > 0 && choice <= (int) types.size())
                                addEffectOfType (types[(size_t) (choice - 1)].id);
                        });
}

namespace
{

/** The presets a slot can be given: its own type's, and nothing else. */
std::vector<Preset> presetsForSlot (const juce::ValueTree& effect)
{
    if (const auto type = effectTypeFor (effect[ids::type].toString()))
        return PresetLibrary::presetsFor (*type);

    return {};
}

} // namespace

juce::StringArray EffectChainComponent::presetMenuItems (int slot) const
{
    juce::StringArray items;

    for (const auto& preset : presetsForSlot (effectAt (slot)))
        items.add (preset.name);

    return items;
}

bool EffectChainComponent::applyPresetChoice (int slot, int choice)
{
    const auto effect = effectAt (slot);
    const auto presets = presetsForSlot (effect);

    if (choice < 1 || choice > (int) presets.size())
        return false;

    return ProjectEdits::applyEffectPreset (effect, presets[(size_t) (choice - 1)],
                                            &document.getUndoManager());
}

void EffectChainComponent::showPresetMenu (int slot, juce::Component& target)
{
    const auto presets = presetsForSlot (effectAt (slot));

    // Nothing to show rather than an empty menu, which reads as broken. The
    // button is disabled for the same reason; this is the guard behind it.
    if (presets.empty())
        return;

    juce::PopupMenu menu;

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        juce::PopupMenu::Item item (presets[(size_t) i].name);
        item.itemID = i + 1;
        menu.addItem (item);
    }

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [this, slot] (int choice) { applyPresetChoice (slot, choice); });
}

void EffectChainComponent::rebuild()
{
    const juce::ScopedValueSetter<bool> quiet (rebuilding, true);

    // A rebuild in the middle of a drag would leave the gesture pointing at
    // cards that no longer exist. Committing one is what CAUSES a rebuild, so
    // this is the far end of that: whatever was being dragged is over.
    reorder = {};
    dropArea = {};

    cards.clear();
    slide.clear();

    int index = 0;

    for (const auto& child : chainOwner)
        if (child.hasType (ids::EFFECT))
            addAndMakeVisible (
                cards.add (new EffectCard (*this, document, editorState, child, index++)));

    for (int i = 0; i < cards.size(); ++i)
    {
        // Driven by the animator, so the cards PART rather than jumping when
        // the drop point moves. onChanged lays out rather than repaints,
        // because repainting a component does not move it.
        auto* motion = slide.add (new ComponentMotion (*this));
        motion->onChanged = [this] { applyCardPositions(); };
    }

    snapNextLayout = true;

    selectedSlot = juce::jlimit (0, juce::jmax (0, cards.size() - 1), selectedSlot);

    // With nothing open, the panel shows only a list of names and no controls -
    // strictly less than the single-selection editor this replaced. So the
    // selected card opens unless the user has opened something themselves.
    auto anyExpanded = false;

    for (auto* card : cards)
        anyExpanded = anyExpanded || card->isExpanded();

    if (! anyExpanded && ! cards.isEmpty())
        editorState.setEffectExpanded (cards[selectedSlot]->getEffectId(), true);

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setSelected (i == selectedSlot);

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

int EffectChainComponent::getRequiredHeight() const
{
    // A row is one card tall whether it holds four effects or none, so the
    // mixer's effect band does not change height as you fill it.
    if (isHorizontal())
        return EffectCard::cardHeight + space::xs + space::sm;

    auto height = 0;

    for (auto* card : cards)
        height += card->getRequiredHeight() + space::xs;

    if (cards.isEmpty())
        height += size::rowHeight;

    return height + space::sm;
}

int EffectChainComponent::getRequiredWidth() const
{
    if (! isHorizontal())
        return getWidth();

    auto width = space::xs;

    for (auto* card : cards)
        width += card->getRequiredWidth() + space::xs;

    return width;
}

int EffectChainComponent::slotAtPosition (juce::Point<int> position) const
{
    for (int i = 0; i < cards.size(); ++i)
        if (isHorizontal() ? position.x < cards[i]->getRight() : position.y < cards[i]->getBottom())
            return i;

    return juce::jmax (0, cards.size() - 1);
}

void EffectChainComponent::notifyRequiredSizeChanged()
{
    if (onRequiredSizeChanged != nullptr)
        onRequiredSizeChanged();
}

void EffectChainComponent::resized()
{
    layOutCards();
}

juce::Rectangle<int> EffectChainComponent::getSlotBounds (int index) const
{
    return juce::isPositiveAndBelow (index, cards.size()) ? cards[index]->getBounds()
                                                          : juce::Rectangle<int>();
}

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

bool EffectChainComponent::keyPressed (const juce::KeyPress& key)
{
    if (isReordering() && hotkeys::viewCommandFor (key) == hotkeys::ViewCommand::clearSelection)
    {
        endReorder (false);
        return true;
    }

    return false;
}

void EffectChainComponent::paint (juce::Graphics& g)
{
    if (! chainOwner.isValid())
    {
        paint::emptyState (g, getLocalBounds().removeFromTop (size::rowHeight * 2),
                           "Nothing selected");
        return;
    }

    if (cards.isEmpty())
    {
        // Across the viewport, not across the content: with no cards the two
        // are the same, and this way the message cannot start off-screen.
        const juce::Rectangle<int> empty { 0, 0, getWidth(), size::rowHeight };

        paint::inertArea (g, empty);

        paint::emptyState (g, empty.reduced (space::md, 0), "No effects yet - use + to add one",
                           juce::Justification::centredLeft);
    }

    // The gap the other cards have opened. Painted HERE rather than over the
    // children, so it stays behind the card being dragged across it: an
    // outline drawn on top of the thing it is describing reads as a border on
    // the card rather than as a hole in the row.
    if (! dropArea.isEmpty())
    {
        const auto area = dropArea.reduced (space::xxs).toFloat();

        g.setColour (colour::surfaceHover);
        g.fillRoundedRectangle (area, radius::md);

        g.setColour (colour::accent);
        g.drawRoundedRectangle (area, radius::md, stroke::regular);
    }
}

void EffectChainComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent == chainOwner || child.hasType (ids::EFFECT))
        rebuild();
}

void EffectChainComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child,
                                                  int)
{
    if (parent == chainOwner || child.hasType (ids::EFFECT))
        rebuild();
}

void EffectChainComponent::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent == chainOwner)
        rebuild();
}

void EffectChainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (! tree.hasType (ids::EFFECT))
        return;

    for (auto* card : cards)
        if (card->getEffectId() == (int) tree[ids::id])
            card->refreshValues();
}

void EffectChainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A card opening or closing changes every card's position and the chain's
    // height, so the host that scrolls it has to be told.
    for (auto* card : cards)
        card->resized();

    resized();
    repaint();
    notifyRequiredSizeChanged();
}

} // namespace dew
