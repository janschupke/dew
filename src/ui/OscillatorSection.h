#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineSnapshot.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "ui/FmMatrixPanel.h"
#include "ui/ParamContextMenu.h"
#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/RotaryGesture.h"

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
    /** The selector's fourth segment: the FM matrix rather than a slot.

        A value of the SAME view state the three slots use, not a boolean
        beside it. Two pieces of state saying which face is on screen is two
        pieces of state that can disagree, and this one already survives a
        channel change and stays out of the document.
    */
    static constexpr int fmTabIndex = kMaxOscillators;

    static constexpr int heightFor (bool wavetableMode, bool lfoOpen) noexcept
    {
        return (wavetableMode ? classicHeight + wavetableExtra : classicHeight) + lfoHeaderExtra
               + (lfoOpen ? lfoOpenExtra : 0);
    }

    /** Every control a GENERATOR owns, beside the property that says whose.

        The pairing IS the panel's whole knowledge of the split now: the
        registry decides what a slot shows and this says which control each of
        its parameters is drawn as. Public so a test can hold the two against
        each other - a control whose property no generator claims would be
        shown for every generator, silently.
    */
    std::vector<std::pair<const juce::Identifier*, juce::Component*>> generatorControls();

    /** Every control the SLOT owns, as against a generator's or the LFO's.

        Public so a test can hold the FM face against it - the matrix is the one
        face where none of these belongs on screen, and "hidden" is the kind of
        claim that is true until somebody adds a tenth control.
    */
    std::vector<juce::Component*> slotControls();

    /** The same pairing for the LFO's controls.

        Separate from generatorControls() and not merged into it: that list is
        held against the GENERATOR registry by a test, and an LFO control has no
        generator to claim it - it belongs to the slot. Merging the two would
        make that gate either fail or stop meaning anything.
    */
    std::vector<std::pair<const juce::Identifier*, juce::Component*>> lfoControls();

    /** The height for the slot currently showing. Asked of the section rather
        than cached by the host, so there is only one copy of the answer and
        nothing for a layout and a refresh to disagree about.
    */
    int getRequiredHeight() const noexcept
    {
        // The matrix is its own face and its own height: it shows the slots
        // TOGETHER, so none of the three answers above describes it.
        if (selectedSlot == fmTabIndex)
            return selectorHeight + rowGap + FmMatrixPanel::preferredHeight;

        return heightFor (showingWavetable, showingLfo);
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
    /** Which generator the slot on show is running - "classic", "wavetable".

        The panel's preset picker asks this: a wavetable's patches are its own,
        and offering them beside the classic ones was a list in which the only
        thing telling the two apart was the sound. */
    juce::String selectedGeneratorId() const;

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

    /** The FM face, so a test drives the twelve cells rather than the widget
        tree they happen to sit in. */
    FmMatrixPanel& getFmMatrix() noexcept
    {
        return fmMatrix;
    }
    juce::Button& getEnableButton() noexcept
    {
        return enableButton;
    }
    juce::Button& getLfoButton() noexcept
    {
        return lfoButton;
    }
    juce::Button& getLfoSyncButton() noexcept
    {
        return lfoSyncButton;
    }
    juce::ComboBox& getLfoWaveBox() noexcept
    {
        return lfoWaveBox;
    }
    juce::ComboBox& getLfoDivisionBox() noexcept
    {
        return lfoDivisionBox;
    }
    DewKnob& getLfoRateKnob() noexcept
    {
        return lfoRateKnob;
    }
    DewKnob& getLfoPanKnob() noexcept
    {
        return lfoPanKnob;
    }
    bool isShowingLfo() const noexcept
    {
        return showingLfo;
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

    /** A CONTROL's height, not a ruler's.

        This was rulerHeight, which is 22, and an IncDecButtons slider takes its
        two halves from two different places: LookAndFeel_V2::getSliderLayout
        centres the text box at the height setTextBoxStyle was given, while
        Slider::Pimpl::resizeIncDecButtons hands the buttons the whole
        component. So the number sat 20 tall between two 22 tall buttons, in a
        panel where every other input is 26.
    */
    static constexpr int octaveHeight = tokens::size::controlHeight;
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

    /** The LFO's switch, shape and sync, which every slot shows. Always there,
        because a movement control nothing hints at is one nobody finds. */
    static constexpr int lfoHeaderExtra = rowGap + formRowHeight;

    /** What opens below it when the LFO is on: the rate - or the division, in
        the same cell - and the three depths.

        Conditional for the reason the wavetable rows are: reserving it in every
        channel that never switches an LFO on spends most of a knob row out of
        the effect chain below, for nothing.
    */
    static constexpr int lfoOpenExtra = rowGap + knobRowHeight;

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
    /** Writes one property of the selected slot, and reports whether it did.

        The answer is what the gesture needs: a write refused because a refresh
        is in progress, or because the slot is not there yet, must not open an
        undo transaction for the next value to join. */
    bool write (const juce::Identifier& property, const juce::var& value,
                const juce::String& transactionName, bool continuing = false);

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

    /** The fourth face, behind the fourth segment. Its own component because
        it is the one thing here that is about every slot at once - see
        FmMatrixPanel. */
    FmMatrixPanel fmMatrix;

    bool showingFm() const noexcept
    {
        return selectedSlot == fmTabIndex;
    }

    DewIconButton enableButton { icons::power(), {} };

    DewDropdown modeBox;

    DewDropdown waveBox;

    DewDropdown tableBox, sourceBox;

    DewSlider octaveSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    DewLabel octaveLabel;

    // Every one of these used to state its range here, a second time. The
    // catalog states it once, and the engine clamps by the same row.
    DewKnob detuneKnob { requireInstrumentParamSpec (ids::detuneCents) };
    DewKnob gainKnob { requireInstrumentParamSpec (ids::gain) };

    DewKnob positionKnob { requireInstrumentParamSpec (ids::wavePosition) };
    DewKnob modKnob { requireInstrumentParamSpec (ids::wavePositionMod) };
    DewKnob rateKnob { requireInstrumentParamSpec (ids::wavePositionRate) };
    DewKnob unisonKnob { requireInstrumentParamSpec (ids::unisonVoices) };
    DewKnob spreadKnob { requireInstrumentParamSpec (ids::unisonDetune) };

    /** The menus for the controls that carry no onContextMenu of their own.

        A DewSlider consumes a popup press and offers nothing, and a DewDropdown
        is a juce::ComboBox with no hook at all, so the octave stepper and the
        four choice boxes are reached by a MouseListener - the same answer
        MixerStrip's fader takes, and for the same reason. Held here because a
        Trigger must outlive the control it watches.
    */
    std::vector<std::unique_ptr<paramMenu::Trigger>> paramMenuTriggers;

    /** The slot's LFO. `lfoRateKnob` and `lfoDivisionBox` share one cell and
        swap on the sync toggle, so the block is one height either way. */
    DewIconButton lfoButton { icons::power(), {} };
    DewLabel lfoLabel;
    DewDropdown lfoWaveBox, lfoDivisionBox;
    DewLetterToggle lfoSyncButton { "S", tokens::colour::accent,
                                    tr (StringId::oscillator_lfoSync_help) };

    DewKnob lfoRateKnob { requireInstrumentParamSpec (ids::lfoRate) };
    DewKnob lfoPitchKnob { requireInstrumentParamSpec (ids::lfoToPitch) };
    DewKnob lfoVolumeKnob { requireInstrumentParamSpec (ids::lfoToVolume) };
    DewKnob lfoPanKnob { requireInstrumentParamSpec (ids::lfoToPan) };

    juce::Rectangle<int> offCaptionBounds, shapeBounds;

    int selectedSlot = 0;
    bool updating = false;

    /** Whether the SELECTED slot is a wavetable. Cached from the document, so
        resized(), paint() and the height the host budgets cannot disagree.
    */
    bool showingWavetable = false;

    /** Whether the selected slot's LFO is on, and whether it is synced. Cached
        from the document for the reason showingWavetable is: resized(), paint()
        and the height the host budgets must not be able to disagree. */
    bool showingLfo = false;
    bool showingSync = false;

    /** True between a knob's onEditStart and onEditEnd - see write(). */
    /** One gesture for the section: the octave stepper and every knob. */
    RotaryGesture gesture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscillatorSection)
};

} // namespace dew
