// =============================================================================
// The oscillator panel's two faces.
//
// The same class, a second translation unit.
//
// A slot is a classic oscillator or a wavetable, and the panel shows a
// different set of controls for each. This is what changes between them: which
// controls exist, what the header says, how they lay out, and - for a wavetable
// - the shape display that draws the frame the position is currently on.
//
// The file next door is the slot machinery: which slot is selected, whether it
// is enabled, and every write to the document. Neither face writes anything.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/OscillatorSection.h"

#include "model/GeneratorCatalog.h"
#include "ui/OscillatorSlot.h"

#include <cmath>

#include "engine/Wavetable.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;
using namespace oscillatorChoices;

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

/** Every control whose parameter belongs to a generator rather than to the
    slot, beside the property that says whose it is.

    The pairing is the point: the registry decides what is shown, and this says
    which control each of its parameters is drawn as. A generator with a
    parameter nothing draws simply has no row here.
*/
std::vector<std::pair<const juce::Identifier*, juce::Component*>>
OscillatorSection::generatorControls()
{
    return { { &ids::wave, &waveBox },
             { &ids::wavetable, &tableBox },
             { &ids::wavePositionSource, &sourceBox },
             { &ids::wavePosition, &positionKnob },
             { &ids::wavePositionMod, &modKnob },
             { &ids::wavePositionRate, &rateKnob },
             { &ids::unisonVoices, &unisonKnob },
             { &ids::unisonDetune, &spreadKnob } };
}

void OscillatorSection::refreshControls()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    const auto slot = selectedSlotTree();
    const auto valid = slot.isValid();

    const auto wasWavetable = showingWavetable;
    const auto generator = valid ? generatorFor (slot[ids::mode].toString()).id : "";
    showingWavetable = valid && juce::String (generator) == "wavetable";

    enableButton.setEnabled (valid);
    modeBox.setEnabled (valid);
    waveBox.setEnabled (valid);
    tableBox.setEnabled (valid);
    sourceBox.setEnabled (valid);
    octaveSlider.setEnabled (valid);
    detuneKnob.setEnabled (valid);
    gainKnob.setEnabled (valid);

    // Which face is on screen is decided by which generator OWNS each control's
    // parameter, asked of the registry. It was a bool and a hand-written list
    // of the seven wavetable controls, so a third generator meant finding this
    // list and remembering what belonged in it - and the bool was set by
    // comparing a stored string to the literal "wavetable".
    for (const auto& [property, control] : generatorControls())
        control->setVisible (valid && ! isForeignGeneratorParam (generator, *property));

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

    // One sentence whatever the state, which is what the other four on/off
    // controls say - "Turn this channel on or off", and the same for a track,
    // an insert and an effect. A tooltip that read the state back was a fifth
    // way of saying the same thing, and the only one that needed two strings.
    enableButton.setTooltip (
        tr (StringId::oscillator_enabled_help, Args {}.with ("index", selectedSlot + 1)));

    modeBox.setSelectedId (idFor (choicesOf (ids::mode), slot[ids::mode].toString()),
                           juce::dontSendNotification);
    waveBox.setSelectedId (idFor (choicesOf (ids::wave), slot[ids::wave].toString()),
                           juce::dontSendNotification);
    sourceBox.setSelectedId (
        idFor (choicesOf (ids::wavePositionSource), slot[ids::wavePositionSource].toString()),
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
        g.drawText (tr (StringId::oscillator_notPlaying), offCaptionBounds,
                    juce::Justification::centredRight, false);
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
