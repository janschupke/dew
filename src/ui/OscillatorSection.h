#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "ui/ParamContextMenu.h"
#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** A channel's oscillator slots, one at a time.

    A compact segmented header - OSC 1 | OSC 2 | OSC 3 - chooses which slot the
    controls below edit, and shows at a glance which slots are switched on.
    Deliberately not a juce::TabbedComponent: its chrome is taller than this
    panel can spare, and a tab has nowhere to say whether the thing behind it is
    making any sound.

    One set of controls, rebound when the selection changes, rather than one set
    per slot. Only one slot is visible at a time, so three sets would be three
    times the widgets for nothing.

    A slot runs one of two generators, chosen by its mode. The classic face is a
    waveform picker; the wavetable face is a table, a morph position and the
    unison stack around it. The two share the octave, detune and gain rows,
    because those mean the same thing either way - the same argument
    InstrumentPanel makes for keeping its synth and audio faces in one panel.

    Which slot is selected is view state, kept in EditorState for the same
    reason the expanded effect cards are - it is not part of the project, must
    not go on the undo stack, and must not make the document dirty. The MODE is
    not view state: it is what the channel sounds like, so it lives in the tree.
*/
class OscillatorSection : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener
{
public:
    /** Hands this section's knobs what a right-click menu needs. Null means no
        menus. Set from above, because a section knows which slot a knob was
        built for and nothing about the playhead. */
    void setParamMenuHost (const paramMenu::Host*);

    OscillatorSection (ProjectDocument&, EditorState&);
    ~OscillatorSection() override;

    /** Points the section at a channel's INSTRUMENT node. An invalid tree
        disables it rather than leaving the previous channel's slots on screen.
    */
    void setOwner (juce::ValueTree instrument);

    /** Height the section needs in each mode.

        Still a constant per mode rather than a measurement - the host has to
        budget before anything is laid out - but no longer a single number. A
        wavetable slot carries five more controls and a shape display, and
        reserving room for them in every classic channel would spend about 200px
        of a 300px-wide panel on nothing, taken straight out of the effect chain
        below. EffectChainComponent::Card already varies its height with its
        mode for the same reason.
    */
    static constexpr int heightFor (bool wavetableMode) noexcept
    {
        return wavetableMode ? classicHeight + wavetableExtra : classicHeight;
    }

    /** The height for the slot currently showing. Asked of the section rather
        than cached by the host, so there is only one copy of the answer and
        nothing for a layout and a refresh to disagree about.
    */
    int getRequiredHeight() const noexcept
    {
        return heightFor (showingWavetable);
    }

    /** Fired when getRequiredHeight() changes - a mode edit, or selecting a
        slot in the other mode. The host has to lay out again; nothing else can
        notice, because the mode lives on a node the host does not listen to.
    */
    std::function<void()> onHeightChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    // --- for tests -----------------------------------------------------------
    int getNumSlots() const;
    int getSelectedSlot() const noexcept
    {
        return selectedSlot;
    }
    void selectSlot (int index);

    bool isSlotEnabled (int index) const;
    bool isShowingWavetable() const noexcept
    {
        return showingWavetable;
    }

    /** What the power button does. Exposed so a test drives the behaviour
        rather than the widget: a DewIconButton flips its own toggle state
        inside Button::internalClickCallback, which a directly-invoked onClick
        never runs, so a test clicking the button would write the stale value.
    */
    void setSlotEnabled (int index, bool shouldBeEnabled);

    juce::Button& getSlotButton (int index) const;
    juce::Button& getEnableButton() noexcept
    {
        return enableButton;
    }
    juce::ComboBox& getWaveBox() noexcept
    {
        return waveBox;
    }
    juce::ComboBox& getModeBox() noexcept
    {
        return modeBox;
    }
    juce::ComboBox& getTableBox() noexcept
    {
        return tableBox;
    }
    juce::ComboBox& getSourceBox() noexcept
    {
        return sourceBox;
    }
    DewKnob& getPositionKnob() noexcept
    {
        return positionKnob;
    }
    DewKnob& getModKnob() noexcept
    {
        return modKnob;
    }
    DewKnob& getRateKnob() noexcept
    {
        return rateKnob;
    }
    DewKnob& getUnisonKnob() noexcept
    {
        return unisonKnob;
    }
    DewKnob& getSpreadKnob() noexcept
    {
        return spreadKnob;
    }

private:
    class SlotButton;

    /** The rows this section stacks. resized() lays them out from these and
        the height sums below add them up, so the two cannot disagree - which
        they could when the sum spelled the ladder numerically: 26 IS
        controlHeight, 22 IS rulerHeight, 24 IS iconButton, and reading the sum
        told you none of that.
    */
    static constexpr int selectorHeight = tokens::size::iconButton;
    static constexpr int octaveHeight = tokens::size::rulerHeight;
    static constexpr int shapeHeight = tokens::size::knob;

    static constexpr int rowGap = tokens::space::sm;
    static constexpr int formRowHeight = tokens::size::controlHeight;
    static constexpr int knobRowHeight = tokens::size::knobRow;

    static constexpr int classicHeight = selectorHeight            // slot selector
                                         + rowGap + formRowHeight  // power + mode
                                         + rowGap + formRowHeight  // wave, or table
                                         + rowGap + octaveHeight   // octave
                                         + rowGap + knobRowHeight; // detune + gain

    static constexpr int wavetableExtra = rowGap + knobRowHeight   // position, mod, rate
                                          + rowGap + knobRowHeight // unison + spread
                                          + rowGap + shapeHeight;  // the shape display

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::ValueTree slotAt (int index) const;
    juce::ValueTree selectedSlotTree() const
    {
        return slotAt (selectedSlot);
    }

    /** Writes to the selected slot, opening one undo transaction per gesture. */
    void write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName);

    /** Wires one wavetable knob to a property. Five knobs of identical shape,
        which is four more than is worth spelling out by hand.
    */
    void attachKnob (DewKnob&, const juce::Identifier& property,
                     const juce::String& transactionName, bool integral = false);

    void refreshHeader();
    void refreshControls();
    void paintShape (juce::Graphics&) const;

    ProjectDocument& document;
    EditorState& editorState;
    const paramMenu::Host* paramMenuHost = nullptr;

    juce::ValueTree instrument;

    juce::OwnedArray<SlotButton> slotButtons;

    DewIconButton enableButton { icons::power(), "Turn this oscillator on or off" };

    DewDropdown modeBox;

    DewDropdown waveBox;

    DewDropdown tableBox, sourceBox;

    juce::Slider octaveSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label octaveLabel;

    // Every one of these used to state its range here, a second time. The
    // catalog states it once, and the engine clamps by the same row.
    DewKnob detuneKnob { requireInstrumentParamSpec (ids::detuneCents) };
    DewKnob gainKnob { requireInstrumentParamSpec (ids::gain) };

    DewKnob positionKnob { requireInstrumentParamSpec (ids::wavePosition) };
    DewKnob modKnob { requireInstrumentParamSpec (ids::wavePositionMod) };
    DewKnob rateKnob { requireInstrumentParamSpec (ids::wavePositionRate) };
    DewKnob unisonKnob { requireInstrumentParamSpec (ids::unisonVoices) };
    DewKnob spreadKnob { requireInstrumentParamSpec (ids::unisonDetune) };

    juce::Rectangle<int> offCaptionBounds, shapeBounds;

    int selectedSlot = 0;
    bool updating = false;

    /** Whether the SELECTED slot is a wavetable. Cached from the document, so
        resized(), paint() and the height the host budgets cannot disagree.
    */
    bool showingWavetable = false;

    /** True between a knob's onEditStart and onEditEnd - see write(). */
    bool inDrag = false;
    bool gestureActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscillatorSection)
};

} // namespace dew
