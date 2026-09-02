#include "OscillatorSection.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "../model/ProjectSchema.h"

namespace dew
{

using namespace tokens;

namespace
{

/** The wave combo's item order, which is also its id order. One table rather
    than the two parallel ladders the panel used to carry.
*/
const char* const waveNames[] = { "sine", "saw", "square", "triangle" };

int waveIdFor (const juce::String& name)
{
    for (int i = 0; i < 4; ++i)
        if (name == waveNames[i])
            return i + 1;

    return 2;   // saw, the schema's default
}

} // namespace

/** One entry in the segmented header.

    A juce::Button rather than a painted rectangle, so it gets hover and press
    for free and so it turns up in the findAll<juce::Button> sweeps the
    selection tests use.
*/
class OscillatorSection::SlotButton : public juce::Button
{
public:
    explicit SlotButton (int i)
        : juce::Button ("OSC " + juce::String (i + 1)), index (i)
    {
        setTooltip ("Edit oscillator " + juce::String (i + 1));
    }

    void setSelected (bool s)    { if (std::exchange (selected, s) != s) repaint(); }
    void setSlotEnabled (bool e) { if (std::exchange (slotEnabled, e) != e) repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto body = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (selected      ? colour::surfaceHover
                     : highlighted ? colour::surfaceRaised.brighter (0.05f)
                                   : colour::surfaceRaised);
        g.fillRoundedRectangle (body, radius::sm);

        g.setColour (selected ? colour::accent : colour::outline);
        g.drawRoundedRectangle (body, radius::sm,
                                selected ? stroke::regular : stroke::hairline);

        // A dot rather than a second word: three of these share the panel's
        // width, and "OSC 1 ON" at a size that still reads does not fit.
        const auto dot = body.removeFromLeft (12.0f).withSizeKeepingCentre (5.0f, 5.0f);
        g.setColour (slotEnabled ? colour::success : colour::textDisabled);
        g.fillEllipse (dot);

        g.setColour (slotEnabled ? colour::textPrimary : colour::textDisabled);
        g.setFont (type::font (type::caption, selected));
        g.drawText (getButtonText(), body.toNearestInt(), juce::Justification::centred, false);
    }

    const int index;

private:
    bool selected = false;
    bool slotEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotButton)
};

// -----------------------------------------------------------------------------

OscillatorSection::OscillatorSection (ProjectDocument& d, EditorState& s)
    : document (d), editorState (s)
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

    for (int i = 0; i < 4; ++i)
        waveBox.addItem (juce::String (waveNames[i]).substring (0, 1).toUpperCase()
                             + juce::String (waveNames[i]).substring (1),
                         i + 1);

    waveBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, 3, waveBox.getSelectedId() - 1);
        write (ids::wave, waveNames[index], "Change waveform");
    };
    addAndMakeVisible (waveBox);

    octaveLabel.setText ("OCT", juce::dontSendNotification);
    octaveLabel.setFont (type::font (type::caption));
    octaveLabel.setColour (juce::Label::textColourId, colour::textSecondary);
    addAndMakeVisible (octaveLabel);

    octaveSlider.setRange (-3, 3, 1);
    octaveSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, size::controlHeightSm);
    octaveSlider.onDragStart = [this]
    {
        document.getUndoManager().beginNewTransaction ("Change octave");
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
    detuneKnob.onEditStart = [this]
    {
        document.getUndoManager().beginNewTransaction ("Change detune");
    };
    detuneKnob.onValueChange = [this]
    {
        // detuneCents is a double in the schema, so it is written as one.
        write (ids::detuneCents, detuneKnob.getValue(), "Change detune");
    };
    addAndMakeVisible (detuneKnob);

    gainKnob.setNumDecimalPlaces (2);
    gainKnob.onEditStart = [this]
    {
        document.getUndoManager().beginNewTransaction ("Change oscillator gain");
    };
    gainKnob.onValueChange = [this]
    {
        write (ids::gain, gainKnob.getValue(), "Change oscillator gain");
    };
    addAndMakeVisible (gainKnob);

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

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction (shouldBeEnabled ? "Enable oscillator" : "Disable oscillator");
    slot.setProperty (ids::enabled, shouldBeEnabled, &undo);
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

    auto& undo = document.getUndoManager();

    // A button or a typed value produces no drag, so there may be no
    // transaction open; beginNewTransaction is a no-op if one already is.
    undo.beginNewTransaction (transactionName);
    slot.setProperty (property, value, &undo);
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

void OscillatorSection::refreshHeader()
{
    const auto slots = getNumSlots();

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        auto* button = slotButtons[i];
        button->setVisible (i < slots);
        button->setSelected (i == selectedSlot);
        button->setSlotEnabled (isSlotEnabled (i));
    }
}

void OscillatorSection::refreshControls()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto slot = selectedSlotTree();
    const auto valid = slot.isValid();

    enableButton.setEnabled (valid);
    waveBox.setEnabled (valid);
    octaveSlider.setEnabled (valid);
    detuneKnob.setEnabled (valid);
    gainKnob.setEnabled (valid);

    if (! valid)
        return;

    const auto enabled = (bool) slot.getProperty (ids::enabled, true);

    enableButton.setToggleState (enabled, juce::dontSendNotification);
    repaint();
    enableButton.setTooltip (enabled ? "Turn oscillator " + juce::String (selectedSlot + 1)
                                           + " off"
                                     : "Turn oscillator " + juce::String (selectedSlot + 1)
                                           + " on");

    waveBox.setSelectedId (waveIdFor (slot[ids::wave].toString()), juce::dontSendNotification);
    octaveSlider.setValue ((double) slot[ids::octave], juce::dontSendNotification);
    detuneKnob.setValue ((double) slot[ids::detuneCents], juce::dontSendNotification);
    gainKnob.setValue ((double) slot[ids::gain], juce::dontSendNotification);
}

void OscillatorSection::paint (juce::Graphics& g)
{
    // A switched-off oscillator still shows its settings rather than an empty
    // panel - they are what you are about to turn on - so it has to say
    // somewhere that nothing you change here is currently audible.
    if (selectedSlotTree().isValid() && ! isSlotEnabled (selectedSlot))
    {
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::caption));
        g.drawText ("NOT PLAYING", offCaptionBounds, juce::Justification::centredRight, false);
    }
}

void OscillatorSection::resized()
{
    auto area = getLocalBounds();

    auto selector = area.removeFromTop (24);
    const auto slotWidth = juce::jmax (1, selector.getWidth() / juce::jmax (1, slotButtons.size()));

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        auto cell = i == slotButtons.size() - 1 ? selector
                                                : selector.removeFromLeft (slotWidth);
        slotButtons[i]->setBounds (cell.reduced (space::xxs, 0));
    }

    area.removeFromTop (space::sm);

    auto waveRow = area.removeFromTop (size::controlHeight);
    enableButton.setBounds (waveRow.removeFromLeft (size::controlHeight));
    waveRow.removeFromLeft (space::xs);
    waveBox.setBounds (waveRow);

    area.removeFromTop (space::sm);

    auto octaveRow = area.removeFromTop (22);
    octaveLabel.setBounds (octaveRow.removeFromLeft (30));

    // Sized rather than stretched: a pair of inc/dec buttons as wide as the
    // panel is not easier to hit, only emptier.
    octaveSlider.setBounds (octaveRow.removeFromLeft (juce::jmin (110, octaveRow.getWidth())));

    // The stepper leaves room on this row, which is where the section says it
    // is not sounding - not over the selector, where a tab button paints on top.
    offCaptionBounds = octaveRow;

    area.removeFromTop (space::sm);

    auto knobs = area.removeFromTop (68);
    detuneKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (space::xxs, 0));
    gainKnob.setBounds (knobs.reduced (space::xxs, 0));
}

} // namespace dew
