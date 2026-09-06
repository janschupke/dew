// =============================================================================
// TransportBar - the metre.
//
// Split out because TransportBar.cpp had reached the four hundred code lines
// the tree allows a file, and this is the seam that was already there: the
// closed list of metres, the box that shows it, and the one edit that changes
// the project's own. Nothing else on the bar reads meterChoices.
//
// It is a metre, not a tempo. Meter (model/Meter.h) is the only place that
// knows the difference, and applyMeterChoice below is why: beatsPerBar rescales
// every clip in the arrangement, so the change and the rescale share one
// transaction or undo puts half of it back.
// =============================================================================

#include "ui/TransportBar.h"

#include "i18n/Strings.h"
#include "model/Meter.h"
#include "model/NoteTools.h"
#include "model/ProjectEdits.h"

namespace dew
{

// Simple metres first, then compound, then the odd ones - the order a musician
// would look for them in rather than numeric order. Every denominator here is a
// note value, which is why the list is closed: 3/6 is not a metre.
const TransportBar::MeterChoice TransportBar::meterChoices[] = { { 4, 4 }, { 3, 4 },  { 2, 4 },
                                                                 { 5, 4 }, { 6, 4 },  { 6, 8 },
                                                                 { 9, 8 }, { 12, 8 }, { 7, 8 },
                                                                 { 5, 8 }, { 2, 2 },  { 3, 8 } };

const int TransportBar::numMeterChoices = (int) (sizeof (meterChoices) / sizeof (meterChoices[0]));

void TransportBar::rebuildMeterList()
{
    const juce::ScopedValueSetter<bool> guard (updatingMeterBox, true);

    meterBox.clear (juce::dontSendNotification);

    // Ids are one-based because a ComboBox reads 0 as "nothing selected".
    for (int i = 0; i < numMeterChoices; ++i)
    {
        const auto& choice = meterChoices[i];
        meterBox.addItem (
            tr (StringId::transport_meter_format,
                Args {}.with ("beats", choice.beatsPerBar).with ("unit", choice.beatUnit)),
            i + 1);
    }
}

void TransportBar::refreshMeter()
{
    const juce::ScopedValueSetter<bool> guard (updatingMeterBox, true);

    const auto meter = Meter::of (document.getState());

    for (int i = 0; i < numMeterChoices; ++i)
    {
        if (meterChoices[i].beatsPerBar == meter.beatsPerBar
            && meterChoices[i].beatUnit == meter.beatUnit)
        {
            meterBox.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }
    }

    // A metre the list does not offer - a file written by hand, or by a later
    // build. Shown as itself rather than snapped to the nearest entry, because
    // the box would otherwise claim the project is something it is not.
    meterBox.addItem (meter.toString(), numMeterChoices + 1);
    meterBox.setSelectedId (numMeterChoices + 1, juce::dontSendNotification);
}

void TransportBar::applyMeterChoice (int itemId)
{
    const auto index = itemId - 1;

    if (index < 0 || index >= numMeterChoices)
        return;

    const auto& choice = meterChoices[index];

    // One transaction for the metre and the rescale it drags behind it, so undo
    // puts the whole arrangement back rather than half of it.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Change time signature");

    auto exact = true;
    ProjectEdits::setMeter (document.getState(), choice.beatsPerBar, choice.beatUnit, &undo,
                            &exact);

    if (onMeterChanged != nullptr)
        onMeterChanged (exact);
}

// --- the grid ----------------------------------------------------------------

void TransportBar::rebuildGridList()
{
    const juce::ScopedValueSetter<bool> guard (updatingGridBox, true);

    gridBox.clear (juce::dontSendNotification);

    const auto beatUnit = Meter::of (document.getState()).beatUnit;

    for (int i = 0; i < NoteTools::numGridResolutions; ++i)
        gridBox.addItem (NoteTools::nameForGrid (NoteTools::gridResolutions[i], beatUnit), i + 1);
}

void TransportBar::refreshGrid()
{
    const juce::ScopedValueSetter<bool> guard (updatingGridBox, true);

    const auto current = Meter::of (document.getState()).stepsPerBeat;

    for (int i = 0; i < NoteTools::numGridResolutions; ++i)
    {
        if (NoteTools::gridResolutions[i] != current)
            continue;

        gridBox.setSelectedId (i + 1, juce::dontSendNotification);
        return;
    }

    // A resolution the list does not offer - a file written by hand, or by a
    // later build. Shown as itself rather than snapped to the nearest, for the
    // reason refreshMeter says: the box would otherwise claim the project is
    // something it is not.
    gridBox.addItem (NoteTools::nameForGrid (current, Meter::of (document.getState()).beatUnit),
                     NoteTools::numGridResolutions + 1);
    gridBox.setSelectedId (NoteTools::numGridResolutions + 1, juce::dontSendNotification);
}

void TransportBar::applyGridChoice (int itemId)
{
    const auto index = itemId - 1;

    if (index < 0 || index >= NoteTools::numGridResolutions)
        return;

    // One transaction for the resolution and the rescale it drags behind it -
    // every note, every pattern length and every automation point - so undo
    // puts the whole document back rather than the setting alone. The metre's
    // own edit is the same shape and for the same reason.
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Change grid resolution");

    ProjectEdits::setGridResolution (document.getState(), NoteTools::gridResolutions[index], &undo);
}

} // namespace dew
