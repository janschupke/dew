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

} // namespace

/** One effect: a header that is always visible, and a body that folds away. */

// -----------------------------------------------------------------------------

EffectChainComponent::EffectChainComponent (ProjectDocument& d, EditorState& s)
    : document (d)
    , editorState (s)
{
    setComponentID ("effectChain");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle ("Effect chain");
    setFocusContainerType (FocusContainerType::focusContainer);

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
