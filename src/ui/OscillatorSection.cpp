#include "ui/OscillatorSection.h"

#include "model/GeneratorCatalog.h"

#include "i18n/Strings.h"
#include "ui/OscillatorSlot.h"

#include "engine/Wavetable.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/design/Cursors.h"
#include "ui/design/Glyphs.h"

namespace dew
{

using namespace tokens;
using namespace oscillatorChoices;

// -----------------------------------------------------------------------------

OscillatorSection::OscillatorSection (ProjectDocument& d, EditorState& s)
    : document (d)
    , editorState (s)
    , fmMatrix (d)
{
    setComponentID ("oscillatorSection");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::oscillator_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    for (int i = 0; i < kMaxOscillators; ++i)
    {
        auto* button = slotButtons.add (new SlotButton (i));
        button->onClick = [this, i] { selectSlot (i); };
        addAndMakeVisible (button);
    }

    // The fourth segment. Built by the same class and laid out by the same
    // loop, so it cannot drift from the three beside it - it simply has no
    // sounding/silent dot to show, because it is not a slot.
    {
        auto* button = slotButtons.add (new SlotButton (
            fmTabIndex, tr (StringId::oscillator_fm_label), tr (StringId::oscillator_fm_help)));
        button->onClick = [this] { selectSlot (fmTabIndex); };
        addAndMakeVisible (button);
    }

    addChildComponent (fmMatrix);

    enableButton.setClickingTogglesState (true);
    enableButton.setOnColour (colour::success);
    enableButton.onClick = [this]
    {
        // Deliberately not reading enableButton.getToggleState(): the button
        // mirrors the document, it is not the source of truth, and reading it
        // is what makes a test that invokes onClick() directly - with no
        // message loop to have flipped it - write the value back unchanged.
        setSlotEnabled (selectedSlot, ! isSlotEnabled (selectedSlot));
    };
    addAndMakeVisible (enableButton);

    fill (modeBox, choicesOf (ids::mode));
    modeBox.setTooltip (tr (StringId::oscillator_mode_help));
    modeBox.onChange = [this]
    {
        write (ids::mode, valueOf (choicesOf (ids::mode), modeBox.getSelectedId()),
               TransactionName { "Change oscillator mode" });
    };
    addAndMakeVisible (modeBox);

    // The four wave icons existed for as long as the icon set has and were
    // drawn by nothing: the one place a person picks a shape showed the four as
    // four words.
    fill (waveBox, choicesOf (ids::wave),
          [] (const char* name) { return glyph::forWaveform (waveformFromString (name)); });
    waveBox.onChange = [this]
    {
        write (ids::wave, valueOf (choicesOf (ids::wave), waveBox.getSelectedId()),
               TransactionName { "Change waveform" });
    };
    waveBox.setTooltip (tr (StringId::oscillator_wave_help));
    addAndMakeVisible (waveBox);

    // The table list comes from the engine's bank rather than a second list
    // here, so adding a factory table needs no change in the panel at all.
    for (int i = 0; i < wavetableCount(); ++i)
        tableBox.addItem (wavetableAt (i).getDisplayName(), i + 1);

    tableBox.setTooltip (tr (StringId::oscillator_table_help));
    tableBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, wavetableCount() - 1, tableBox.getSelectedId() - 1);
        write (ids::wavetable, wavetableAt (index).getName(),
               TransactionName { "Change wavetable" });
    };
    addAndMakeVisible (tableBox);

    fill (sourceBox, choicesOf (ids::wavePositionSource));
    sourceBox.setTooltip (tr (StringId::oscillator_source_help));
    sourceBox.onChange = [this]
    {
        write (ids::wavePositionSource,
               valueOf (choicesOf (ids::wavePositionSource), sourceBox.getSelectedId()),
               TransactionName { "Change modulation source" });
    };
    addAndMakeVisible (sourceBox);

    octaveLabel.setText (tr (StringId::oscillator_octave_caption), juce::dontSendNotification);
    octaveLabel.setFont (type::font (type::caption));
    octaveLabel.setTextColourToken (colour::textSecondary);
    addAndMakeVisible (octaveLabel);

    {
        // Four octaves either way, which is what the engine renders; this
        // stepper offered three, so the top of the range was unreachable.
        const auto& spec = requireInstrumentParamSpec (ids::octave);
        octaveSlider.setRange (spec.minimum, spec.maximum, spec.interval);
    }

    octaveSlider.setMouseCursor (cursor::clickable);
    // The FULL height of the row, so the number and the two buttons beside it
    // are one control rather than three things that happen to be adjacent -
    // see OscillatorSection::octaveHeight.
    octaveSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, size::controlHeight);
    gesture.attach (octaveSlider,
                    [this] (bool continuing)
                    {
                        // Integer-valued in the file: writing a double would change the
                        // JSON from `0` to `0.0` and hand the rounding to the schema's
                        // coercion.
                        return write (ids::octave, (int) octaveSlider.getValue(),
                                      TransactionName { "Change octave" }, continuing);
                    });
    addAndMakeVisible (octaveSlider);

    attachKnob (detuneKnob, ids::detuneCents, TransactionName { "Change detune" });

    attachKnob (gainKnob, ids::gain, TransactionName { "Change oscillator gain" });

    positionKnob.setTooltip (tr (StringId::oscillator_position_help));
    attachKnob (positionKnob, ids::wavePosition, TransactionName { "Change wavetable position" });

    modKnob.setTooltip (tr (StringId::oscillator_mod_help));
    attachKnob (modKnob, ids::wavePositionMod, TransactionName { "Change position modulation" });

    rateKnob.setTooltip (tr (StringId::oscillator_rate_help));
    attachKnob (rateKnob, ids::wavePositionRate, TransactionName { "Change modulation rate" });

    unisonKnob.setTooltip (tr (StringId::oscillator_unison_help));

    // Integral, which is what the trailing true says: the file keeps an int,
    // the same rule the octave stepper follows and for the same reason.
    attachKnob (unisonKnob, ids::unisonVoices, TransactionName { "Change unison voices" }, true);

    spreadKnob.setTooltip (tr (StringId::oscillator_spread_help));
    attachKnob (spreadKnob, ids::unisonDetune, TransactionName { "Change unison spread" });

    // --- the slot's LFO ------------------------------------------------------
    lfoButton.setClickingTogglesState (true);
    lfoButton.setOnColour (colour::accent);
    lfoButton.onClick = [this]
    {
        // The document, not the button's own state - see enableButton above for
        // why reading the widget back is what breaks a headless test.
        const auto lfo = generatorNodeFor (selectedSlotTree(), ids::lfoOn);
        write (ids::lfoOn, ! (bool) lfo.getProperty (ids::lfoOn, false),
               TransactionName { "Switch LFO on or off" });
    };
    addAndMakeVisible (lfoButton);

    // The row is otherwise a switch, a shape and a letter, sitting directly
    // under the oscillator's own shape picker - which reads as a second one.
    // The caption is what says whose shape it is, and the octave row above
    // already sets the pattern.
    lfoLabel.setText (tr (StringId::group_lfo_name), juce::dontSendNotification);
    lfoLabel.setFont (type::font (type::caption));
    lfoLabel.setTextColourToken (colour::textSecondary);
    addAndMakeVisible (lfoLabel);

    fill (lfoWaveBox, choicesOf (ids::lfoWave),
          [] (const char* name) { return glyph::forWaveform (waveformFromString (name)); });
    lfoWaveBox.setTooltip (tr (StringId::oscillator_lfoWave_help));
    lfoWaveBox.onChange = [this]
    {
        write (ids::lfoWave, valueOf (choicesOf (ids::lfoWave), lfoWaveBox.getSelectedId()),
               TransactionName { "Change LFO shape" });
    };
    addAndMakeVisible (lfoWaveBox);

    lfoSyncButton.setClickingTogglesState (true);
    lfoSyncButton.onClick = [this]
    {
        const auto lfo = generatorNodeFor (selectedSlotTree(), ids::lfoSync);
        write (ids::lfoSync, ! (bool) lfo.getProperty (ids::lfoSync, false),
               TransactionName { "Sync the LFO" });
    };
    addAndMakeVisible (lfoSyncButton);

    fill (lfoDivisionBox, choicesOf (ids::lfoDivision));
    lfoDivisionBox.setTooltip (tr (StringId::oscillator_lfoDivision_help));
    lfoDivisionBox.onChange = [this]
    {
        write (ids::lfoDivision,
               valueOf (choicesOf (ids::lfoDivision), lfoDivisionBox.getSelectedId()),
               TransactionName { "Change LFO division" });
    };
    addAndMakeVisible (lfoDivisionBox);

    lfoRateKnob.setTooltip (tr (StringId::oscillator_lfoRate_help));
    attachKnob (lfoRateKnob, ids::lfoRate, TransactionName { "Change LFO rate" });

    lfoPitchKnob.setTooltip (tr (StringId::oscillator_lfoToPitch_help));
    attachKnob (lfoPitchKnob, ids::lfoToPitch, TransactionName { "Change LFO pitch depth" });

    lfoVolumeKnob.setTooltip (tr (StringId::oscillator_lfoToVolume_help));
    attachKnob (lfoVolumeKnob, ids::lfoToVolume, TransactionName { "Change LFO volume depth" });

    lfoPanKnob.setTooltip (tr (StringId::oscillator_lfoToPan_help));
    attachKnob (lfoPanKnob, ids::lfoToPan, TransactionName { "Change LFO pan depth" });

    selectedSlot = juce::jlimit (0, fmTabIndex, editorState.getSelectedOscillator());

    editorState.addChangeListener (this);
    document.getState().addListener (this);

    refresh();
}

OscillatorSection::~OscillatorSection()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void OscillatorSection::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The SLOT, asked for fresh: this section re-points at another oscillator
    // without rebuilding its knobs, so a captured tree would edit whichever slot
    // happened to be selected when the host arrived.
    const auto slot = [this] { return selectedSlotTree(); };

    // A generator's knob points at that generator's node, not at the slot -
    // asked the same way every read and write in this file asks.
    const auto owner = [this] (const juce::Identifier& property)
    { return [this, &property] { return generatorNodeFor (selectedSlotTree(), property); }; };

    paramMenu::attachTo (host, detuneKnob, slot, requireInstrumentParamSpec (ids::detuneCents));
    paramMenu::attachTo (host, gainKnob, slot, requireInstrumentParamSpec (ids::gain));
    paramMenu::attachTo (host, positionKnob, owner (ids::wavePosition),
                         requireInstrumentParamSpec (ids::wavePosition));
    paramMenu::attachTo (host, modKnob, owner (ids::wavePositionMod),
                         requireInstrumentParamSpec (ids::wavePositionMod));
    paramMenu::attachTo (host, rateKnob, owner (ids::wavePositionRate),
                         requireInstrumentParamSpec (ids::wavePositionRate));
    paramMenu::attachTo (host, unisonKnob, owner (ids::unisonVoices),
                         requireInstrumentParamSpec (ids::unisonVoices));
    paramMenu::attachTo (host, spreadKnob, owner (ids::unisonDetune),
                         requireInstrumentParamSpec (ids::unisonDetune));

    // The LFO's node is found the same way a generator's is, because
    // generatorNodeFor answers for every node under the slot.
    paramMenu::attachTo (host, lfoRateKnob, owner (ids::lfoRate),
                         requireInstrumentParamSpec (ids::lfoRate));
    paramMenu::attachTo (host, lfoPitchKnob, owner (ids::lfoToPitch),
                         requireInstrumentParamSpec (ids::lfoToPitch));
    paramMenu::attachTo (host, lfoVolumeKnob, owner (ids::lfoToVolume),
                         requireInstrumentParamSpec (ids::lfoToVolume));
    paramMenu::attachTo (host, lfoPanKnob, owner (ids::lfoToPan),
                         requireInstrumentParamSpec (ids::lfoToPan));

    // The rest of the slot, which had no menu at all - so the octave stepper,
    // the on/off and the four choice boxes were the only spec-built controls in
    // the panel with no "reset to default", and three of them are automatable.
    paramMenu::attachTo (host, enableButton, slot, requireInstrumentParamSpec (ids::enabled));

    paramMenuTriggers.clear();

    if (host == nullptr || host->document == nullptr)
        return;

    const auto trigger = [this, host] (juce::Component& control,
                                       std::function<juce::ValueTree()> node,
                                       const juce::Identifier& property)
    {
        paramMenuTriggers.push_back (std::make_unique<paramMenu::Trigger> (
            control, host->contextFor (std::move (node), requireInstrumentParamSpec (property))));
    };

    trigger (octaveSlider, slot, ids::octave);
    trigger (modeBox, slot, ids::mode);
    trigger (waveBox, owner (ids::wave), ids::wave);
    trigger (tableBox, owner (ids::wavetable), ids::wavetable);
    trigger (sourceBox, owner (ids::wavePositionSource), ids::wavePositionSource);

    // Twelve more spec-built knobs, each pinned to a fixed slot rather than to
    // the selected one - so the matrix wires its own.
    fmMatrix.setParamMenuHost (host);
}

void OscillatorSection::attachKnob (DewKnob& knob, const juce::Identifier& property,
                                    TransactionName transactionName, bool integral)
{
    gesture.attach (
        knob,
        [this, &knob, property, transactionName, integral] (bool continuing)
        {
            return integral ? write (property, (int) knob.getValue(), transactionName, continuing)
                            : write (property, knob.getValue(), transactionName, continuing);
        });

    addAndMakeVisible (knob);
}

void OscillatorSection::setOwner (juce::ValueTree newInstrument)
{
    if (instrument == newInstrument)
        return;

    instrument = std::move (newInstrument);
    fmMatrix.setOwner (instrument);
    refresh();
}

int OscillatorSection::getNumSlots() const
{
    int count = 0;

    for (const auto& node : instrument)
        if (node.hasType (ids::OSC))
            ++count;

    return count;
}

juce::String OscillatorSection::selectedGeneratorId() const
{
    // Through the registry, so a slot naming a generator this build has not got
    // reads as the first one rather than as a kind of its own.
    return generatorFor (selectedSlotTree()[ids::mode].toString()).id;
}

juce::ValueTree OscillatorSection::slotAt (int index) const
{
    if (index < 0)
        return {};

    int seen = 0;

    for (const auto& node : instrument)
        if (node.hasType (ids::OSC) && seen++ == index)
            return node;

    return {};
}

void OscillatorSection::selectSlot (int index)
{
    const auto clamped = juce::jlimit (0, fmTabIndex, index);

    if (clamped == selectedSlot)
        return;

    selectedSlot = clamped;

    // EditorState broadcasts, which brings us back through
    // changeListenerCallback and refreshes - so no refresh() here.
    editorState.setSelectedOscillator (clamped);
}

bool OscillatorSection::isSlotEnabled (int index) const
{
    const auto slot = slotAt (index);
    return slot.isValid() && (bool) slot.getProperty (ids::enabled, true);
}

void OscillatorSection::setSlotEnabled (int index, bool shouldBeEnabled)
{
    auto slot = slotAt (index);

    if (! slot.isValid() || isSlotEnabled (index) == shouldBeEnabled)
        return;

    ProjectEdits::setProperty (
        slot, ids::enabled, shouldBeEnabled, &document.getUndoManager(),
        TransactionName { shouldBeEnabled ? "Enable oscillator" : "Disable oscillator" });
}

juce::Button& OscillatorSection::getSlotButton (int index) const
{
    return *slotButtons[juce::jlimit (0, slotButtons.size() - 1, index)];
}

bool OscillatorSection::write (const juce::Identifier& property, const juce::var& value,
                               TransactionName transactionName, bool continuing)
{
    if (updating)
        return false;

    const auto slot = selectedSlotTree();

    // The node the parameter actually lives on: the slot for its own five, and
    // the owning generator's child for anything else. ONE write path, so
    // nesting them was this line rather than every control.
    const auto target = generatorNodeFor (slot, property);

    if (! target.isValid())
        return false;

    ProjectEdits::setProperty (target, property, value, &document.getUndoManager(), transactionName,
                               continuing);

    // Whether the gesture may advance. A write that did not happen must not
    // open one, or the next value would join a transaction nothing started.
    return true;
}

void OscillatorSection::changeListenerCallback (juce::ChangeBroadcaster*)
{
    selectedSlot = juce::jlimit (0, fmTabIndex, editorState.getSelectedOscillator());
    refresh();
}

void OscillatorSection::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier& property)
{
    // Identity, not type: the listener is on the whole document, and every
    // channel in it carries nodes of this type.
    //
    // A slot's own node, or one of the nodes UNDER it - its two generators and
    // its LFO. Accepting only the slot meant that an undo, a preset load or an
    // MCP write to a generator's node changed the document and left the panel
    // showing what it used to say, with nothing to notice; every control in
    // this panel that is not one of the slot's own five was affected.
    const auto slot = isOscChildNode (tree) ? tree.getParent() : tree;

    if (! slot.hasType (ids::OSC) || slot.getParent() != instrument)
        return;

    // The header shows every slot's switch; the controls show one slot's
    // settings. Keeping them apart is what stops a change to slot 3 from
    // re-reading, and momentarily flickering, the knobs showing slot 1.
    if (property == ids::enabled)
        refreshHeader();

    if (slot == selectedSlotTree())
        refreshControls();
}

void OscillatorSection::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent == instrument)
        refresh();
}

void OscillatorSection::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent == instrument)
        refresh();
}

void OscillatorSection::refresh()
{
    refreshHeader();
    refreshControls();
}

} // namespace dew
