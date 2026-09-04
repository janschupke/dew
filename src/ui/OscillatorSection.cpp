#include "ui/OscillatorSection.h"

#include "engine/Wavetable.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;

namespace
{

/** A combo box's items: the string the document stores, and what the panel
    calls it. One table per box rather than two parallel ladders, so an item's
    id is its index in the table and nothing has to be kept in step by hand.
*/
struct NamedChoice
{
    const char* value;
    const char* display;
};

const NamedChoice waveChoices[] {
    { "sine", "Sine" }, { "saw", "Saw" }, { "square", "Square" }, { "triangle", "Triangle" }
};

const NamedChoice modeChoices[] { { "classic", "Classic" }, { "wavetable", "Wavetable" } };

// "LFO", not "Lfo": capitalising the stored name works for every other box here
// and would be wrong for exactly this one.
const NamedChoice sourceChoices[] { { "envelope", "Envelope" }, { "lfo", "LFO" } };

/** The 1-based combo id of a stored name, falling back to the schema default.

    `fallback` is a VALUE, not an index, so a caller names the default it wants
    the way the schema does rather than counting rows to find it.
*/
template <size_t N>
int idFor (const NamedChoice (&choices)[N], const juce::String& name, const char* fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (name == choices[i].value)
            return (int) i + 1;

    for (size_t i = 0; i < N; ++i)
        if (juce::String (fallback) == choices[i].value)
            return (int) i + 1;

    return 1;
}

template <size_t N> void fill (juce::ComboBox& box, const NamedChoice (&choices)[N])
{
    for (size_t i = 0; i < N; ++i)
        box.addItem (choices[i].display, (int) i + 1);
}

template <size_t N> const char* valueOf (const NamedChoice (&choices)[N], int selectedId)
{
    return choices[(size_t) juce::jlimit (0, (int) N - 1, selectedId - 1)].value;
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
        : juce::Button ("OSC " + juce::String (i + 1))
        , index (i)
    {
        setTooltip ("Edit oscillator " + juce::String (i + 1));
    }

    /** The same rule the dew primitives follow: juce::Button completes a click
        for whichever mouse button pressed it, and a right-click on a slot tab
        asked for nothing. */
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            return;

        juce::Button::mouseDown (event);
    }

    void setSelected (bool s)
    {
        if (std::exchange (selected, s) != s)
            repaint();
    }
    void setSlotEnabled (bool e)
    {
        if (std::exchange (slotEnabled, e) != e)
            repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto body = paint::bodyRect (*this);

        g.setColour (selected      ? colour::surfaceHover
                     : highlighted ? colour::surfaceRaised.brighter (emphasis::surfaceLift)
                                   : colour::surfaceRaised);
        g.fillRoundedRectangle (body, radius::sm);

        g.setColour (selected ? colour::accent : colour::outline);
        g.drawRoundedRectangle (body, radius::sm, selected ? stroke::regular : stroke::hairline);

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

    const auto wasWavetable = showingWavetable;
    showingWavetable = valid && slot[ids::mode].toString() == "wavetable";

    enableButton.setEnabled (valid);
    modeBox.setEnabled (valid);
    waveBox.setEnabled (valid);
    tableBox.setEnabled (valid);
    sourceBox.setEnabled (valid);
    octaveSlider.setEnabled (valid);
    detuneKnob.setEnabled (valid);
    gainKnob.setEnabled (valid);

    // Exactly one face is on screen at a time, decided here and nowhere else -
    // resized() and paint() both read the cached answer.
    waveBox.setVisible (valid && ! showingWavetable);

    const std::initializer_list<juce::Component*> wavetableOnly {
        &tableBox, &sourceBox, &positionKnob, &modKnob, &rateKnob, &unisonKnob, &spreadKnob
    };

    for (auto* c : wavetableOnly)
        c->setVisible (valid && showingWavetable);

    if (wasWavetable != showingWavetable)
    {
        resized();

        // The host budgets this section's height before laying anything out,
        // and the mode lives on a node it does not listen to - so it has to be
        // told, or the new rows would be laid out beyond the bottom edge.
        if (onHeightChanged != nullptr)
            onHeightChanged();
    }

    if (! valid)
        return;

    const auto enabled = (bool) slot.getProperty (ids::enabled, true);

    enableButton.setToggleState (enabled, juce::dontSendNotification);
    repaint();
    enableButton.setTooltip (enabled
                                 ? "Turn oscillator " + juce::String (selectedSlot + 1) + " off"
                                 : "Turn oscillator " + juce::String (selectedSlot + 1) + " on");

    modeBox.setSelectedId (idFor (modeChoices, slot[ids::mode].toString(), "classic"),
                           juce::dontSendNotification);
    waveBox.setSelectedId (idFor (waveChoices, slot[ids::wave].toString(), "saw"),
                           juce::dontSendNotification);
    sourceBox.setSelectedId (
        idFor (sourceChoices, slot[ids::wavePositionSource].toString(), "envelope"),
        juce::dontSendNotification);

    // A table name this build does not know shows as the first one, which is
    // also what the engine falls back to - the panel must not disagree with
    // what is actually sounding.
    const auto table = wavetableIndexFor (slot[ids::wavetable].toString());
    tableBox.setSelectedId (juce::jmax (0, table) + 1, juce::dontSendNotification);

    octaveSlider.setValue ((double) slot[ids::octave], juce::dontSendNotification);
    detuneKnob.setValue ((double) slot[ids::detuneCents], juce::dontSendNotification);
    gainKnob.setValue ((double) slot[ids::gain], juce::dontSendNotification);

    positionKnob.setValue ((double) slot[ids::wavePosition], juce::dontSendNotification);
    modKnob.setValue ((double) slot[ids::wavePositionMod], juce::dontSendNotification);
    rateKnob.setValue ((double) slot[ids::wavePositionRate], juce::dontSendNotification);
    unisonKnob.setValue ((double) (int) slot[ids::unisonVoices], juce::dontSendNotification);
    spreadKnob.setValue ((double) slot[ids::unisonDetune], juce::dontSendNotification);
}

void OscillatorSection::paintShape (juce::Graphics& g) const
{
    paint::wellBackground (g, shapeBounds);

    const auto slot = selectedSlotTree();

    if (! slot.isValid())
        return;

    const auto index = wavetableIndexFor (slot[ids::wavetable].toString());
    const auto& table = wavetableAt (juce::jmax (0, index));
    const auto position = (float) (double) slot[ids::wavePosition];

    const auto area = shapeBounds.reduced (space::xs).toFloat();
    const auto columns = juce::jmax (2, (int) area.getWidth());

    // One cycle across the width, read from the full-band mip. The knob above
    // says which position it is; this says what that position sounds like,
    // which no number does.
    juce::Path path;

    for (int x = 0; x < columns; ++x)
    {
        const auto phase = (double) x / (double) columns;
        const auto value = table.at (position, 0, phase);

        const auto px = area.getX() + area.getWidth() * (float) phase;
        const auto py = area.getCentreY() - value * area.getHeight() * 0.45f;

        if (x == 0)
            path.startNewSubPath (px, py);
        else
            path.lineTo (px, py);
    }

    g.setColour (isSlotEnabled (selectedSlot) ? colour::accent : colour::textDisabled);
    g.strokePath (path, juce::PathStrokeType (stroke::regular));
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

    if (showingWavetable && ! shapeBounds.isEmpty())
        paintShape (g);
}

void OscillatorSection::resized()
{
    auto area = getLocalBounds();

    auto selector = area.removeFromTop (selectorHeight);
    const auto slotWidth = juce::jmax (1, selector.getWidth() / juce::jmax (1, slotButtons.size()));

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        auto cell = i == slotButtons.size() - 1 ? selector : selector.removeFromLeft (slotWidth);
        slotButtons[i]->setBounds (cell.reduced (space::xxs, 0));
    }

    area.removeFromTop (space::sm);

    auto modeRow = area.removeFromTop (formRowHeight);
    enableButton.setBounds (modeRow.removeFromLeft (size::controlHeight));
    modeRow.removeFromLeft (space::xs);
    modeBox.setBounds (modeRow);

    area.removeFromTop (space::sm);

    // The two faces share this row: a classic slot's waveform, or a wavetable
    // slot's table and what modulates its position.
    auto generatorRow = area.removeFromTop (formRowHeight);

    if (showingWavetable)
    {
        auto left = generatorRow.removeFromLeft (generatorRow.getWidth() * 3 / 5);
        tableBox.setBounds (left);
        generatorRow.removeFromLeft (space::xs);
        sourceBox.setBounds (generatorRow);
    }
    else
    {
        waveBox.setBounds (generatorRow);
    }

    area.removeFromTop (space::sm);

    auto octaveRow = area.removeFromTop (octaveHeight);
    octaveLabel.setBounds (octaveRow.removeFromLeft (30));

    // Sized rather than stretched: a pair of inc/dec buttons as wide as the
    // panel is not easier to hit, only emptier.
    octaveSlider.setBounds (octaveRow.removeFromLeft (juce::jmin (110, octaveRow.getWidth())));

    // The stepper leaves room on this row, which is where the section says it
    // is not sounding - not over the selector, where a tab button paints on top.
    offCaptionBounds = octaveRow;

    area.removeFromTop (space::sm);

    const auto knobRow = [&area] (int count, std::initializer_list<DewKnob*> knobs)
    {
        auto row = area.removeFromTop (knobRowHeight);
        const auto width = juce::jmax (1, row.getWidth() / juce::jmax (1, count));
        int placed = 0;

        for (auto* knob : knobs)
        {
            auto cell = ++placed == count ? row : row.removeFromLeft (width);
            knob->setBounds (cell.reduced (space::xxs, 0));
        }
    };

    knobRow (2, { &detuneKnob, &gainKnob });

    if (! showingWavetable)
    {
        shapeBounds = {};
        return;
    }

    area.removeFromTop (space::sm);
    knobRow (3, { &positionKnob, &modKnob, &rateKnob });

    area.removeFromTop (space::sm);
    knobRow (2, { &unisonKnob, &spreadKnob });

    area.removeFromTop (space::sm);
    shapeBounds = area.removeFromTop (shapeHeight);
}

} // namespace dew
