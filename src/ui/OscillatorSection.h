#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../engine/EngineSnapshot.h"
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
    int getRequiredHeight() const noexcept { return heightFor (showingWavetable); }

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
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int index);

    bool isSlotEnabled (int index) const;
    bool isShowingWavetable() const noexcept { return showingWavetable; }

    /** What the power button does. Exposed so a test drives the behaviour
        rather than the widget: a DewIconButton flips its own toggle state
        inside Button::internalClickCallback, which a directly-invoked onClick
        never runs, so a test clicking the button would write the stale value.
    */
    void setSlotEnabled (int index, bool shouldBeEnabled);

    juce::Button& getSlotButton (int index) const;
    juce::Button& getEnableButton() noexcept { return enableButton; }
    juce::ComboBox& getWaveBox() noexcept   { return waveBox; }
    juce::ComboBox& getModeBox() noexcept   { return modeBox; }
    juce::ComboBox& getTableBox() noexcept  { return tableBox; }
    juce::ComboBox& getSourceBox() noexcept { return sourceBox; }
    DewKnob& getPositionKnob() noexcept     { return positionKnob; }
    DewKnob& getModKnob() noexcept          { return modKnob; }
    DewKnob& getRateKnob() noexcept         { return rateKnob; }
    DewKnob& getUnisonKnob() noexcept       { return unisonKnob; }
    DewKnob& getSpreadKnob() noexcept       { return spreadKnob; }

private:
    class SlotButton;

    static constexpr int classicHeight = 24                          // slot selector
                                       + tokens::space::sm + 26      // power + mode
                                       + tokens::space::sm + 26      // wave, or table
                                       + tokens::space::sm + 22      // octave
                                       + tokens::space::sm + 68;     // detune + gain

    static constexpr int wavetableExtra = tokens::space::sm + 68     // position, mod, rate
                                        + tokens::space::sm + 68     // unison + spread
                                        + tokens::space::sm + 44;    // the shape display

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::ValueTree slotAt (int index) const;
    juce::ValueTree selectedSlotTree() const { return slotAt (selectedSlot); }

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
    juce::ValueTree instrument;

    juce::OwnedArray<SlotButton> slotButtons;

    DewIconButton enableButton { icons::power(), "Turn this oscillator on or off" };

    juce::ComboBox modeBox;

    juce::ComboBox waveBox;

    juce::ComboBox tableBox, sourceBox;

    juce::Slider octaveSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label octaveLabel;

    // Detune spans one semitone either way. The engine clamps at twelve, but a
    // knob covering two octaves cannot be nudged by a cent, which is the only
    // thing anyone detunes an oscillator by.
    DewKnob detuneKnob { "DETUNE", -100.0, 100.0, 1.0 };
    DewKnob gainKnob   { "GAIN", 0.0, 1.0, 0.01 };

    DewKnob positionKnob { "POSITION", 0.0, 1.0, 0.01 };
    DewKnob modKnob      { "MOD", -1.0, 1.0, 0.01 };
    DewKnob rateKnob     { "RATE", 0.01, 20.0, 0.01 };
    DewKnob unisonKnob   { "UNISON", 1.0, (double) kMaxUnisonVoices, 1.0 };
    DewKnob spreadKnob   { "SPREAD", 0.0, 50.0, 0.5 };

    juce::Rectangle<int> offCaptionBounds, shapeBounds;

    int selectedSlot = 0;
    bool updating = false;

    /** Whether the SELECTED slot is a wavetable. Cached from the document, so
        resized(), paint() and the height the host budgets cannot disagree.
    */
    bool showingWavetable = false;

    /** True between a knob's onEditStart and onEditEnd - see write(). */
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscillatorSection)
};

} // namespace dew
