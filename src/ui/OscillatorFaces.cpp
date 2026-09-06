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
#include "ui/KnobGrid.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewKnob.h"
#include "ui/primitives/DewPaint.h"

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

        // The FM segment is not one of the slots, so it is neither hidden when
        // a channel carries fewer of them nor asked whether it is sounding.
        button->setVisible (! button->isSlot || i < slots);
        button->setSelected (i == selectedSlot);

        if (button->isSlot)
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

/** Every control the SLOT owns, whichever generator it runs and whether or not
    its LFO is open.

    A list rather than nine lines repeated, because it is asked twice - once to
    hide them behind the matrix and once so a test can prove they went. The
    generator's own and the LFO's have their own lists for the same reason.
*/
std::vector<juce::Component*> OscillatorSection::slotControls()
{
    return { &enableButton, &modeBox,   &octaveLabel, &octaveSlider, &detuneKnob,
             &gainKnob,     &lfoButton, &lfoLabel,    &lfoWaveBox,   &lfoSyncButton };
}

std::vector<std::pair<const juce::Identifier*, juce::Component*>> OscillatorSection::lfoControls()
{
    // The switch, the shape and the sync are NOT here: those three are the
    // header row, which every slot shows whether its LFO is on or not. This is
    // what opens below them, and what the height budget turns on.
    return { { &ids::lfoRate, &lfoRateKnob },
             { &ids::lfoDivision, &lfoDivisionBox },
             { &ids::lfoToPitch, &lfoPitchKnob },
             { &ids::lfoToVolume, &lfoVolumeKnob },
             { &ids::lfoToPan, &lfoPanKnob } };
}

void OscillatorSection::refreshControls()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    // The matrix is a face of its own: every control below belongs to ONE slot,
    // and the matrix is the view of all three at once. Showing it means showing
    // nothing else, and `slot` is not a question it has an answer to.
    const auto wasFm = fmMatrix.isVisible();
    const auto fm = showingFm();

    fmMatrix.setVisible (fm);

    if (fm)
        fmMatrix.refresh();

    // The rows that are on screen whichever generator a slot runs. Hidden
    // rather than merely disabled: a disabled control still paints, and on this
    // face there is a matrix where it would be.
    for (auto* control : slotControls())
        control->setVisible (! fm);

    const auto slot = selectedSlotTree();
    const auto valid = slot.isValid() && ! fm;

    const auto wasWavetable = showingWavetable;
    const auto generator = valid ? generatorFor (slot[ids::mode].toString()).id : "";
    showingWavetable = valid && juce::String (generator) == "wavetable";

    const auto lfo = valid ? generatorNodeFor (slot, ids::lfoOn) : juce::ValueTree();

    const auto wasLfo = showingLfo;
    const auto wasSync = showingSync;
    showingLfo = valid && (bool) lfo.getProperty (ids::lfoOn, false);
    showingSync = valid && (bool) lfo.getProperty (ids::lfoSync, false);

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

    lfoButton.setEnabled (valid);
    lfoLabel.setEnabled (valid);
    lfoWaveBox.setEnabled (valid);
    lfoSyncButton.setEnabled (valid);

    // The rate and the division share a cell and swap, so exactly one of them
    // is ever on screen.
    for (const auto& [property, control] : lfoControls())
        control->setVisible (showingLfo && (*property != ids::lfoRate || ! showingSync)
                             && (*property != ids::lfoDivision || showingSync));

    // Three states, one condition. Leaving the LFO out of this was the whole
    // failure mode: switching it on would lay its row out below the section's
    // own bottom edge, where nothing paints it, and the controls would simply
    // not appear.
    if (wasFm != fm || wasWavetable != showingWavetable || wasLfo != showingLfo
        || wasSync != showingSync)
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
    // Each generator's parameters live on its own node under the slot.
    const auto classic = generatorNodeFor (slot, ids::wave);
    const auto wavetable = generatorNodeFor (slot, ids::wavePosition);

    waveBox.setSelectedId (idFor (choicesOf (ids::wave), classic[ids::wave].toString()),
                           juce::dontSendNotification);
    sourceBox.setSelectedId (
        idFor (choicesOf (ids::wavePositionSource), wavetable[ids::wavePositionSource].toString()),
        juce::dontSendNotification);

    // A table name this build does not know shows as the first one, which is
    // also what the engine falls back to - the panel must not disagree with
    // what is actually sounding.
    const auto table = wavetableIndexFor (wavetable[ids::wavetable].toString());
    tableBox.setSelectedId (juce::jmax (0, table) + 1, juce::dontSendNotification);

    octaveSlider.setValue ((double) slot[ids::octave], juce::dontSendNotification);
    detuneKnob.setValue ((double) slot[ids::detuneCents], juce::dontSendNotification);
    gainKnob.setValue ((double) slot[ids::gain], juce::dontSendNotification);

    positionKnob.setValue ((double) wavetable[ids::wavePosition], juce::dontSendNotification);
    modKnob.setValue ((double) wavetable[ids::wavePositionMod], juce::dontSendNotification);
    rateKnob.setValue ((double) wavetable[ids::wavePositionRate], juce::dontSendNotification);
    unisonKnob.setValue ((double) (int) wavetable[ids::unisonVoices], juce::dontSendNotification);
    spreadKnob.setValue ((double) wavetable[ids::unisonDetune], juce::dontSendNotification);

    lfoButton.setToggleState (showingLfo, juce::dontSendNotification);
    lfoButton.setTooltip (tr (StringId::oscillator_lfoOn_help));
    lfoSyncButton.setToggleState (showingSync, juce::dontSendNotification);

    lfoWaveBox.setSelectedId (idFor (choicesOf (ids::lfoWave), lfo[ids::lfoWave].toString()),
                              juce::dontSendNotification);
    lfoDivisionBox.setSelectedId (
        idFor (choicesOf (ids::lfoDivision), lfo[ids::lfoDivision].toString()),
        juce::dontSendNotification);

    lfoRateKnob.setValue ((double) lfo[ids::lfoRate], juce::dontSendNotification);
    lfoPitchKnob.setValue ((double) lfo[ids::lfoToPitch], juce::dontSendNotification);
    lfoVolumeKnob.setValue ((double) lfo[ids::lfoToVolume], juce::dontSendNotification);
    lfoPanKnob.setValue ((double) lfo[ids::lfoToPan], juce::dontSendNotification);
}

void OscillatorSection::paintShape (juce::Graphics& g) const
{
    paint::wellBackground (g, shapeBounds);

    const auto slot = selectedSlotTree();

    if (! slot.isValid())
        return;

    const auto wavetable = generatorNodeFor (slot, ids::wavePosition);
    const auto index = wavetableIndexFor (wavetable[ids::wavetable].toString());
    const auto& table = wavetableAt (juce::jmax (0, index));
    const auto position = (float) (double) wavetable[ids::wavePosition];

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
    // Under the children, which is what paint() is for: the band is the ground
    // its controls stand on rather than an outline drawn round them.
    if (! lfoBandBounds.isEmpty())
    {
        paint::wellBackground (g, lfoBandBounds);

        g.setColour (colour::dividerStrong);
        g.drawHorizontalLine (lfoBandBounds.getY(), 0.0f, (float) getWidth());

        if (! lfoGroupRule.isEmpty())
        {
            g.setColour (colour::divider);
            g.drawVerticalLine (lfoGroupRule.getCentreX(), (float) lfoGroupRule.getY(),
                                (float) lfoGroupRule.getBottom());
        }
    }

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

    // The matrix takes everything below the selector and nothing else is laid
    // out at all: the rows below belong to one slot, and this face is the view
    // of all three.
    if (showingFm())
    {
        fmMatrix.setBounds (area.removeFromTop (FmMatrixPanel::preferredHeight));
        offCaptionBounds = {};
        shapeBounds = {};
        lfoBandBounds = {};
        lfoGroupRule = {};
        return;
    }

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

    // One row of knobs, at the cell width every other knob in the panel gets
    // and centred in what is left. The count argument this lost was how two
    // knobs were made to occupy three columns so the rows below would line up;
    // a cell width shared across the whole panel does that by construction, and
    // does it between panels as well as within one.
    const auto knobRow = [&area] (std::initializer_list<DewKnob*> knobs)
    {
        const std::vector<int> group { (int) knobs.size() };
        const auto plan = KnobGrid::planForRows (1, group, area.getWidth());
        const auto placed = KnobGrid::place (area.removeFromTop (knobRowHeight), plan);

        auto cell = placed.cells.begin();

        for (auto* knob : knobs)
            if (cell != placed.cells.end())
                knob->setBounds (*cell++);
    };

    knobRow ({ &detuneKnob, &gainKnob });

    if (showingWavetable)
    {
        area.removeFromTop (space::sm);
        knobRow ({ &positionKnob, &modKnob, &rateKnob });

        area.removeFromTop (space::sm);
        knobRow ({ &unisonKnob, &spreadKnob });

        area.removeFromTop (space::sm);
        shapeBounds = area.removeFromTop (shapeHeight);
    }
    else
    {
        shapeBounds = {};
    }

    // The LFO goes LAST, below the shape display, so the generator's own
    // controls stay where they were whether it is on or not - a block that
    // pushed them down when it opened would move every knob the hand is
    // already reaching for.
    //
    // Being last is also what lets it be a BAND: a region of the panel running
    // from a rule to the bottom edge, with its own ground, rather than a box
    // drawn around a group. That is the shape this tree uses everywhere - see
    // the tombstone on paint::container - and the rule and the ground are what
    // say the dropdown and the four knobs below it are one thing. The gap that
    // was already here becomes the band's top padding, so nothing moves.
    lfoBandBounds = { 0, area.getY(), getWidth(), getHeight() - area.getY() };

    area.removeFromTop (space::sm);

    auto lfoRow = area.removeFromTop (formRowHeight);
    lfoButton.setBounds (lfoRow.removeFromLeft (size::controlHeight));
    lfoRow.removeFromLeft (space::xs);
    lfoLabel.setBounds (lfoRow.removeFromLeft (30));
    lfoSyncButton.setBounds (lfoRow.removeFromRight (lfoSyncButton.preferredHeight()));
    lfoRow.removeFromRight (space::xs);
    lfoWaveBox.setBounds (lfoRow);

    if (! showingLfo)
    {
        lfoGroupRule = {};
        return;
    }

    area.removeFromTop (space::sm);

    // Two groups, not one: the RATE is what the LFO is doing and the three
    // depths are what it is doing it TO, and KnobGrid puts a rule between two
    // groups sharing a row. Four cells either way, so the depths do not shift
    // sideways when the sync toggle swaps the knob for the division box.
    const std::vector<int> lfoGroup { 1, 3 };
    const auto lfoCells = KnobGrid::place (area.removeFromTop (knobRowHeight),
                                           KnobGrid::planForRows (1, lfoGroup, area.getWidth()));

    lfoGroupRule = lfoCells.rules.empty() ? juce::Rectangle<int> {} : lfoCells.rules.front();

    if (lfoCells.cells.size() == 4)
    {
        // The box is a form control in a knob-height cell, so it sits centred
        // rather than stretched - a dropdown as tall as a knob reads as a
        // different kind of thing entirely.
        lfoDivisionBox.setBounds (
            lfoCells.cells[0].withSizeKeepingCentre (lfoCells.cells[0].getWidth(), formRowHeight));
        lfoRateKnob.setBounds (lfoCells.cells[0]);
        lfoPitchKnob.setBounds (lfoCells.cells[1]);
        lfoVolumeKnob.setBounds (lfoCells.cells[2]);
        lfoPanKnob.setBounds (lfoCells.cells[3]);
    }
}
} // namespace dew
