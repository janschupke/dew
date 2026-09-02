#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ProjectDocument.h"
#include "EditorState.h"
#include "primitives/DewControls.h"

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
    times the widgets for nothing, and the panel's height would depend on which
    slot happened to be open.

    Which slot is selected is view state, kept in EditorState for the same
    reason the expanded effect cards are - it is not part of the project, must
    not go on the undo stack, and must not make the document dirty.
*/
class OscillatorSection : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener
{
public:
    OscillatorSection (ProjectDocument&, EditorState&);
    ~OscillatorSection() override;

    /** Points the section at a channel's INSTRUMENT node. An invalid tree
        disables it rather than leaving the previous channel's slots on screen.
    */
    void setOwner (juce::ValueTree instrument);

    /** Height this section needs. A constant rather than a measurement: it has
        no scrolling content, and its host has to budget for it before anything
        has been laid out.
    */
    static constexpr int requiredHeight = 24                          // slot selector
                                        + tokens::space::sm + 26      // power + wave
                                        + tokens::space::sm + 22      // octave
                                        + tokens::space::sm + 68;     // detune + gain

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    // --- for tests -----------------------------------------------------------
    int getNumSlots() const;
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int index);

    bool isSlotEnabled (int index) const;

    /** What the power button does. Exposed so a test drives the behaviour
        rather than the widget: a DewIconButton flips its own toggle state
        inside Button::internalClickCallback, which a directly-invoked onClick
        never runs, so a test clicking the button would write the stale value.
    */
    void setSlotEnabled (int index, bool shouldBeEnabled);

    juce::Button& getSlotButton (int index) const;
    juce::Button& getEnableButton() noexcept { return enableButton; }
    juce::ComboBox& getWaveBox() noexcept { return waveBox; }

private:
    class SlotButton;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::ValueTree slotAt (int index) const;
    juce::ValueTree selectedSlotTree() const { return slotAt (selectedSlot); }

    /** Writes to the selected slot, opening one undo transaction per gesture. */
    void write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName);

    void refreshHeader();
    void refreshControls();

    ProjectDocument& document;
    EditorState& editorState;
    juce::ValueTree instrument;

    juce::OwnedArray<SlotButton> slotButtons;

    DewIconButton enableButton { icons::power(), "Turn this oscillator on or off" };

    juce::ComboBox waveBox;
    juce::Slider octaveSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label octaveLabel;

    // Detune spans one semitone either way. The engine clamps at twelve, but a
    // knob covering two octaves cannot be nudged by a cent, which is the only
    // thing anyone detunes an oscillator by.
    DewKnob detuneKnob { "DETUNE", -100.0, 100.0, 1.0 };
    DewKnob gainKnob   { "GAIN", 0.0, 1.0, 0.01 };

    juce::Rectangle<int> offCaptionBounds;

    int selectedSlot = 0;
    bool updating = false;

    /** True between a knob's onEditStart and onEditEnd - see write(). */
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscillatorSection)
};

} // namespace dew
