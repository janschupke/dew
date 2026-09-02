#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "primitives/DewControls.h"
#include "primitives/DewNumberField.h"

namespace dew
{

/** The effect chain of one channel or mixer track, as an accordion.

    Deliberately owner-agnostic: a channel and a mixer track carry the same
    EFFECT children, so the same editor drives both rather than two that can
    drift apart. Point it at a node with setOwner().

    Each effect is a card. Its header is always visible - grip, bypass, icon,
    name, expand chevron, reorder, remove - and clicking the header opens the
    card in place. Several can be open at once, which is the point: comparing a
    filter's cutoff against a delay's time used to mean clicking between them.

    Which cards are open is view state, kept in EditorState against the effect's
    id, so it survives a rebuild, is not on the undo stack, and does not make the
    project dirty.
*/
class EffectChainComponent : public juce::Component,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener
{
public:
    EffectChainComponent (ProjectDocument&, EditorState&);
    ~EffectChainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Points the editor at a channel or mixer track. An invalid tree shows the
        empty state rather than the previous owner's chain.
    */
    void setOwner (juce::ValueTree owner);
    const juce::ValueTree& getOwner() const noexcept { return chainOwner; }

    /** A name for the chain, shown in the heading: "EFFECTS - Insert 1". */
    void setOwnerName (juce::String);

    /** Height the whole chain needs. Its host puts this in a Viewport, so a full
        chain scrolls rather than being silently clipped.
    */
    int getRequiredHeight() const;

    // --- for tests -----------------------------------------------------------
    int getNumSlotRows() const { return cards.size(); }
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int index);
    void addEffectOfType (const juce::String& type);

    /** Opens or closes one card, by position in the chain. */
    void setSlotExpanded (int index, bool expanded);
    bool isSlotExpanded (int index) const;

    /** Moves a card by dragging its grip, in the same terms the grip uses. */
    void moveSlot (int from, int to);

private:
    class Card;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuild();
    void showAddMenu();
    void notifyHeightChanged();

    juce::ValueTree effectAt (int index) const;

    static constexpr int headingHeight = 26;

    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree chainOwner;
    juce::String ownerName;

    juce::OwnedArray<Card> cards;
    DewIconButton addButton { icons::plus(), "Add an effect" };

    int selectedSlot = 0;
    bool rebuilding = false;

public:
    /** Called when the chain's required height changes, so a host that scrolls
        it can resize its content. Set by whoever owns the Viewport.
    */
    std::function<void()> onRequiredHeightChanged;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChainComponent)
};

} // namespace dew
