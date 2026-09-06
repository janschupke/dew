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
               "Change oscillator mode");
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
               "Change waveform");
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
        write (ids::wavetable, wavetableAt (index).getName(), "Change wavetable");
    };
    addAndMakeVisible (tableBox);

    fill (sourceBox, choicesOf (ids::wavePositionSource));
    sourceBox.setTooltip (tr (StringId::oscillator_source_help));
    sourceBox.onChange = [this]
    {
        write (ids::wavePositionSource,
               valueOf (choicesOf (ids::wavePositionSource), sourceBox.getSelectedId()),
               "Change modulation source");
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
    positionKnob.setTooltip (tr (StringId::oscillator_position_help));
    attachKnob (positionKnob, ids::wavePosition, "Change wavetable position");

    modKnob.setBipolar (true);
    modKnob.setNumDecimalPlaces (2);
    modKnob.setTooltip (tr (StringId::oscillator_mod_help));
    attachKnob (modKnob, ids::wavePositionMod, "Change position modulation");

    rateKnob.setNumDecimalPlaces (2);
    rateKnob.setTooltip (tr (StringId::oscillator_rate_help));
    attachKnob (rateKnob, ids::wavePositionRate, "Change modulation rate");

    // Integral, so the file keeps an int: the same rule the octave stepper
    // follows, for the same reason.
    unisonKnob.setNumDecimalPlaces (0);
    unisonKnob.setTooltip (tr (StringId::oscillator_unison_help));
    attachKnob (unisonKnob, ids::unisonVoices, "Change unison voices", true);

    spreadKnob.setNumDecimalPlaces (1);
    spreadKnob.setTooltip (tr (StringId::oscillator_spread_help));
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

    const auto slot = selectedSlotTree();

    // The node the parameter actually lives on: the slot for its own five, and
    // the owning generator's child for anything else. ONE write path, so
    // nesting them was this line rather than every control.
    const auto target = generatorNodeFor (slot, property);

    if (! target.isValid())
        return;

    ProjectEdits::setProperty (target, property, value, &document.getUndoManager(), transactionName,
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
