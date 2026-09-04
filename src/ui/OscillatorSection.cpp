#include "ui/OscillatorSection.h"

#include "ui/OscillatorSlot.h"

#include "engine/Wavetable.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;
using namespace oscillatorChoices;

// -----------------------------------------------------------------------------

OscillatorSection::OscillatorSection (ProjectDocument& d, EditorState& s)
    : document (d)
    , editorState (s)
{
    setComponentID ("oscillatorSection");

    for (int i = 0; i < kMaxOscillators; ++i)
    {
        auto* button = slotButtons.add (new SlotButton (i));
        button->onClick = [this, i] { selectSlot (i); };
        addAndMakeVisible (button);
    }

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

    fill (modeBox, modeChoices);
    modeBox.setTooltip ("Which generator this oscillator runs");
    modeBox.onChange = [this]
    {
        write (ids::mode, valueOf (modeChoices, modeBox.getSelectedId()), "Change oscillator mode");
    };
    addAndMakeVisible (modeBox);

    fill (waveBox, waveChoices);
    waveBox.onChange = [this]
    { write (ids::wave, valueOf (waveChoices, waveBox.getSelectedId()), "Change waveform"); };
    waveBox.setTooltip ("The waveform this oscillator plays");
    addAndMakeVisible (waveBox);

    // The table list comes from the engine's bank rather than a second list
    // here, so adding a factory table needs no change in the panel at all.
    for (int i = 0; i < wavetableCount(); ++i)
        tableBox.addItem (wavetableAt (i).getDisplayName(), i + 1);

    tableBox.setTooltip ("Which wavetable this oscillator reads");
    tableBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, wavetableCount() - 1, tableBox.getSelectedId() - 1);
        write (ids::wavetable, wavetableAt (index).getName(), "Change wavetable");
    };
    addAndMakeVisible (tableBox);

    fill (sourceBox, sourceChoices);
    sourceBox.setTooltip ("What moves the position over the length of a note");
    sourceBox.onChange = [this]
    {
        write (ids::wavePositionSource, valueOf (sourceChoices, sourceBox.getSelectedId()),
               "Change modulation source");
    };
    addAndMakeVisible (sourceBox);

    octaveLabel.setText ("OCT", juce::dontSendNotification);
    octaveLabel.setFont (type::font (type::caption));
    octaveLabel.setColour (juce::Label::textColourId, colour::textSecondary);
    addAndMakeVisible (octaveLabel);

    {
        // Four octaves either way, which is what the engine renders; this
        // stepper offered three, so the top of the range was unreachable.
        const auto& spec = requireInstrumentParamSpec (ids::octave);
        octaveSlider.setRange (spec.minimum, spec.maximum, spec.interval);
    }

    octaveSlider.setMouseCursor (cursor::clickable);
    octaveSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, size::controlHeightSm);
    octaveSlider.onDragStart = [this]
    {
        inDrag = true;
        gestureActive = false;
    };
    octaveSlider.onDragEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };
    octaveSlider.onValueChange = [this]
    {
        // Integer-valued in the file: writing a double would change the JSON
        // from `0` to `0.0` and hand the rounding to the schema's coercion.
        write (ids::octave, (int) octaveSlider.getValue(), "Change octave");
    };
    addAndMakeVisible (octaveSlider);

    detuneKnob.setBipolar (true);
    detuneKnob.setNumDecimalPlaces (0);
    attachKnob (detuneKnob, ids::detuneCents, "Change detune");

    gainKnob.setNumDecimalPlaces (2);
    attachKnob (gainKnob, ids::gain, "Change oscillator gain");

    positionKnob.setNumDecimalPlaces (2);
    positionKnob.setTooltip ("Where in the table this oscillator sits");
    attachKnob (positionKnob, ids::wavePosition, "Change wavetable position");

    modKnob.setBipolar (true);
    modKnob.setNumDecimalPlaces (2);
    modKnob.setTooltip ("How far the source moves the position, and which way");
    attachKnob (modKnob, ids::wavePositionMod, "Change position modulation");

    rateKnob.setNumDecimalPlaces (2);
    rateKnob.setTooltip ("Speed of the position LFO, in Hz");
    attachKnob (rateKnob, ids::wavePositionRate, "Change modulation rate");

    // Integral, so the file keeps an int: the same rule the octave stepper
    // follows, for the same reason.
    unisonKnob.setNumDecimalPlaces (0);
    unisonKnob.setTooltip ("How many detuned copies of this oscillator to stack");
    attachKnob (unisonKnob, ids::unisonVoices, "Change unison voices", true);

    spreadKnob.setNumDecimalPlaces (1);
    spreadKnob.setTooltip ("How far apart the unison copies are detuned, in cents");
    attachKnob (spreadKnob, ids::unisonDetune, "Change unison spread");

    selectedSlot = juce::jlimit (0, kMaxOscillators - 1, editorState.getSelectedOscillator());

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

    paramMenu::attachTo (host, detuneKnob, slot, requireInstrumentParamSpec (ids::detuneCents));
    paramMenu::attachTo (host, gainKnob, slot, requireInstrumentParamSpec (ids::gain));
    paramMenu::attachTo (host, positionKnob, slot, requireInstrumentParamSpec (ids::wavePosition));
    paramMenu::attachTo (host, modKnob, slot, requireInstrumentParamSpec (ids::wavePositionMod));
    paramMenu::attachTo (host, rateKnob, slot, requireInstrumentParamSpec (ids::wavePositionRate));
    paramMenu::attachTo (host, unisonKnob, slot, requireInstrumentParamSpec (ids::unisonVoices));
    paramMenu::attachTo (host, spreadKnob, slot, requireInstrumentParamSpec (ids::unisonDetune));
}

void OscillatorSection::attachKnob (DewKnob& knob, const juce::Identifier& property,
                                    const juce::String& transactionName, bool integral)
{
    knob.onEditStart = [this]
    {
        inDrag = true;
        gestureActive = false;
    };
    knob.onEditEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };
    knob.onValueChange = [this, &knob, property, transactionName, integral]
    {
        if (integral)
            write (property, (int) knob.getValue(), transactionName);
        else
            write (property, knob.getValue(), transactionName);
    };

    addAndMakeVisible (knob);
}

void OscillatorSection::setOwner (juce::ValueTree newInstrument)
{
    if (instrument == newInstrument)
        return;

    instrument = std::move (newInstrument);
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
    const auto clamped = juce::jlimit (0, kMaxOscillators - 1, index);

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

    ProjectEdits::setProperty (slot, ids::enabled, shouldBeEnabled, &document.getUndoManager(),
                               shouldBeEnabled ? "Enable oscillator" : "Disable oscillator");
}

juce::Button& OscillatorSection::getSlotButton (int index) const
{
    return *slotButtons[juce::jlimit (0, slotButtons.size() - 1, index)];
}

void OscillatorSection::write (const juce::Identifier& property, const juce::var& value,
                               const juce::String& transactionName)
{
    if (updating)
        return;

    auto slot = selectedSlotTree();

    if (! slot.isValid())
        return;

    ProjectEdits::setProperty (slot, property, value, &document.getUndoManager(), transactionName,
                               gestureActive);

    gestureActive = inDrag;
}

void OscillatorSection::changeListenerCallback (juce::ChangeBroadcaster*)
{
    selectedSlot = juce::jlimit (0, kMaxOscillators - 1, editorState.getSelectedOscillator());
    refresh();
}

void OscillatorSection::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier& property)
{
    // Identity, not type: the listener is on the whole document, and every
    // channel in it carries nodes of this type.
    if (! tree.hasType (ids::OSC) || tree.getParent() != instrument)
        return;

    // The header shows every slot's switch; the controls show one slot's
    // settings. Keeping them apart is what stops a change to slot 3 from
    // re-reading, and momentarily flickering, the knobs showing slot 1.
    if (property == ids::enabled)
        refreshHeader();

    if (tree == selectedSlotTree())
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
