// =============================================================================
// The FM matrix's twelve cells.
//
// Four catalog rows applied to three different nodes, which is why the knobs
// are owned in a vector rather than declared as twelve members: eleven of them
// would be the same declaration with a different index in it.
//
// Nothing here knows what FM sounds like. A cell writes a number onto an
// oscillator slot through ProjectEdits, the reader turns the twelve into a
// bank-wide `anyFm`, and the voice decides what to do with them.
// =============================================================================

#include "ui/FmMatrixPanel.h"

#include "i18n/Strings.h"

#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/InstrumentType.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/KnobGrid.h"
#include "ui/design/Glyphs.h"
#include "ui/design/Icons.h"

namespace dew
{

using namespace tokens;

const juce::Identifier& FmMatrixPanel::propertyForColumn (int column) noexcept
{
    switch (column)
    {
        case 0: return ids::fmTo1;
        case 1: return ids::fmTo2;
        case 2: return ids::fmTo3;
        default: break;
    }

    return ids::fmOut;
}

FmMatrixPanel::FmMatrixPanel (ProjectDocument& d)
    : document (d)
{
    setComponentID ("fmMatrixPanel");

    // A name and a place in the tree a screen reader is given, exactly as the
    // section above it declares one. focusContainer and not the keyboard kind:
    // the second confines the tab key with no key to leave.
    setTitle (tr (StringId::oscillator_fm_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    for (int row = 0; row < kMaxOscillators; ++row)
    {
        for (int column = 0; column < numColumns; ++column)
        {
            const auto& property = propertyForColumn (column);

            auto knob = std::make_unique<DewKnob> (requireInstrumentParamSpec (property));

            // The catalog's own name would be the same sentence in all three
            // rows - a column says what it routes TO and nothing about what it
            // routes FROM. So each cell says both, which is also the accessible
            // name: setTooltip sets that too on every primitive.
            knob->setTooltip (
                column == numColumns - 1
                    ? tr (StringId::oscillator_fm_output_help, Args {}.with ("from", row + 1))
                    : tr (StringId::oscillator_fm_route_help,
                          Args {}.with ("from", row + 1).with ("to", column + 1)));

            auto* raw = knob.get();

            // One undo step per gesture: the first write of a drag opens a
            // transaction and every write after it joins the same one. The two
            // early returns below are why the write reports back - a cell whose
            // slot is not there yet must not leave the gesture looking open.
            gesture.attach (*raw,
                            [this, raw, row, &property] (bool continuing)
                            {
                                if (updating)
                                    return false;

                                const auto slot = slotAt (row);

                                if (! slot.isValid())
                                    return false;

                                ProjectEdits::setProperty (slot, property, raw->getValue(),
                                                           &document.getUndoManager(),
                                                           "Change FM routing", continuing);
                                return true;
                            });

            addAndMakeVisible (raw);
            cells.push_back (std::move (knob));
        }
    }
}

FmMatrixPanel::~FmMatrixPanel()
{
    instrument.removeListener (this);
}

DewKnob& FmMatrixPanel::cellAt (int row, int column) const
{
    const auto index = (size_t) juce::jlimit (0, (int) cells.size() - 1, row * numColumns + column);
    return *cells[index];
}

void FmMatrixPanel::setParamMenuHost (const paramMenu::Host* host)
{
    for (int row = 0; row < kMaxOscillators; ++row)
    {
        for (int column = 0; column < numColumns; ++column)
        {
            const auto& property = propertyForColumn (column);

            // The slot asked for FRESH, the way every other panel asks: this
            // component is re-pointed at another channel without rebuilding its
            // knobs, so a captured tree would edit whichever channel happened
            // to be selected when the host arrived.
            paramMenu::attachTo (
                host, cellAt (row, column), [this, row] { return slotAt (row); },
                requireInstrumentParamSpec (property));
        }
    }
}

juce::ValueTree FmMatrixPanel::slotAt (int index) const
{
    if (index < 0)
        return {};

    int seen = 0;

    for (const auto& node : instrument)
        if (node.hasType (ids::OSC) && seen++ == index)
            return node;

    return {};
}

void FmMatrixPanel::setOwner (juce::ValueTree newInstrument)
{
    if (newInstrument == instrument)
        return;

    instrument.removeListener (this);
    instrument = std::move (newInstrument);
    instrument.addListener (this);

    refresh();
}

void FmMatrixPanel::valueTreePropertyChanged (juce::ValueTree& tree,
                                              const juce::Identifier& property)
{
    // The row labels carry each slot's waveform, and `wave` lives on the
    // CLASSIC node UNDER the slot rather than on the slot - so this has to be
    // asked before the hasType gate below, which that node does not pass.
    if (property == ids::wave || property == ids::mode)
    {
        repaint();
        return;
    }

    if (! tree.hasType (ids::OSC))
        return;

    for (int column = 0; column < numColumns; ++column)
        if (property == propertyForColumn (column))
        {
            refresh();
            return;
        }
}

void FmMatrixPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    for (int row = 0; row < kMaxOscillators; ++row)
    {
        const auto slot = slotAt (row);
        const auto valid = slot.isValid();

        for (int column = 0; column < numColumns; ++column)
        {
            const auto& property = propertyForColumn (column);
            const auto& spec = requireInstrumentParamSpec (property);

            auto& knob = cellAt (row, column);

            knob.setEnabled (valid);
            knob.setValue (valid ? (double) slot.getProperty (property, spec.defaultVar())
                                 : spec.defaultValue,
                           juce::dontSendNotification);
        }
    }

    repaint();
}

void FmMatrixPanel::resized()
{
    rowLabelBounds.clear();

    auto area = getLocalBounds();

    // A gutter wide enough for the two stacked lines that identify a row - the
    // slot's waveform over its name. It was a glyphColumn holding one digit.
    const auto gutter = size::fmRowLabel;

    // Three groups of four, planned ONCE for all three rows, so every row gets
    // the same cell width and the columns line up. Three separate one-row plans
    // would each be centred in the same width and happen to agree - which is a
    // property of them all having four cells, not one this panel could rely on.
    const std::vector<int> groups { numColumns, numColumns, numColumns };
    const auto plan = KnobGrid::planForRows (kMaxOscillators, groups, area.getWidth() - gutter);
    const auto placed = KnobGrid::place (area.withTrimmedLeft (gutter), plan);

    if ((int) placed.cells.size() != kMaxOscillators * numColumns)
        return;

    for (int i = 0; i < (int) placed.cells.size(); ++i)
        cells[(size_t) i]->setBounds (placed.cells[(size_t) i]);

    for (int row = 0; row < kMaxOscillators; ++row)
    {
        const auto& first = placed.cells[(size_t) (row * numColumns)];
        rowLabelBounds.push_back (
            juce::Rectangle<int> (area.getX(), first.getY(), gutter, first.getHeight()));
    }
}

void FmMatrixPanel::paint (juce::Graphics& g)
{
    // Each row says which oscillator it is and what that oscillator sounds
    // like, and there is still no row of column headings: every knob carries
    // the catalog's own caption - TO 1, TO 2, TO 3, OUT - on every row, so a
    // heading strip would say each column's name a second time and spend a row
    // of the narrowest panel in the application doing it.
    //
    // The gutter used to hold a bare digit, on the argument that "OSC" three
    // times would not fit across it. Stacking is what makes it fit: the word
    // goes under the mark rather than beside it, and the matrix is the one
    // face in the section showing all three slots at once - so it is the one
    // place that can show three waveforms side by side.
    g.setFont (type::font (type::caption));

    for (int row = 0; row < (int) rowLabelBounds.size(); ++row)
    {
        auto cell = rowLabelBounds[(size_t) row].withSizeKeepingCentre (
            size::fmRowLabel, size::glyphMark + space::xs + size::captionBand);

        const auto slot = slotAt (row);

        // Asked of the catalog rather than by comparing the stored mode to the
        // literal "wavetable" - the same reason OscillatorSection stopped doing
        // that. Both generator nodes are always present and one is inert, so a
        // wavetable slot still carries a `wave` this would otherwise draw.
        const auto& generator = generatorFor (slot[ids::mode].toString());
        const auto classic = generator.node != nullptr && *generator.node == ids::CLASSIC;

        auto mark = cell.removeFromTop (size::glyphMark);

        if (classic)
        {
            const auto wave = waveformFromString (
                generatorNodeFor (slot, ids::wave)[ids::wave].toString());

            icons::draw (g, glyph::forWaveform (wave), mark.toFloat(), colour::textSecondary);
        }

        cell.removeFromTop (space::xs);

        g.setColour (colour::textPrimary);
        g.drawText (tr (StringId::oscillator_fm_row_label, Args {}.with ("index", row + 1)), cell,
                    juce::Justification::centred, false);
    }
}

} // namespace dew
