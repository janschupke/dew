#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ProjectDocument.h"
#include "primitives/DewControls.h"
#include "primitives/DewNumberField.h"

namespace dew
{

/** The effect chain of one channel or mixer track.

    Deliberately owner-agnostic: a channel and a mixer track carry the same
    EFFECT children, so the same editor drives both rather than two that can
    drift apart. Point it at a node with setOwner().

    Slots are listed with their enable toggle and reorder and remove buttons; the
    selected slot's parameters appear below it. Which parameters those are
    depends on the effect type, and the rows are rebuilt when it changes.
*/
class EffectChainComponent : public juce::Component,
                             private juce::ValueTree::Listener
{
public:
    explicit EffectChainComponent (ProjectDocument&);
    ~EffectChainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Points the editor at a channel or mixer track. An invalid tree shows the
        empty state rather than the previous owner's chain.
    */
    void setOwner (juce::ValueTree owner);
    const juce::ValueTree& getOwner() const noexcept { return chainOwner; }

    /** Height this chain needs, so a host panel can lay out around it. */
    int getRequiredHeight() const;

    // --- for tests -----------------------------------------------------------
    int getNumSlotRows() const { return slotRows.size(); }
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int index);
    void addEffectOfType (const juce::String& type);

private:
    class SlotRow;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void rebuild();
    void rebuildParameters();
    void showAddMenu();

    juce::ValueTree effectAt (int index) const;

    /** One parameter control: a caption, a draggable number field, and the
        property it writes.
    */
    struct ParamControl
    {
        std::unique_ptr<DewNumberField> field;
        juce::Identifier property;
    };

    static constexpr int rowHeight = 24;
    static constexpr int headerHeight = 22;
    static constexpr int paramRowHeight = 38;

    ProjectDocument& document;
    juce::ValueTree chainOwner;

    juce::OwnedArray<SlotRow> slotRows;
    juce::OwnedArray<ParamControl> params;
    std::unique_ptr<juce::ComboBox> modeBox;

    DewIconButton addButton { icons::plus(), "Add an effect" };

    int selectedSlot = 0;
    bool rebuilding = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChainComponent)
};

} // namespace dew
