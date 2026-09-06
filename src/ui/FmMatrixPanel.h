#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ProjectDocument.h"
#include "model/ModuleCatalog.h"
#include "ui/ParamContextMenu.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"
#include "ui/primitives/RotaryGesture.h"

namespace dew
{

/** The FM matrix: a row per oscillator slot, a column per destination, and an
    output column at the end.

    A face of the oscillator section rather than a panel of its own, reached by
    a fourth segment on the same selector - OSC 1 | OSC 2 | OSC 3 | FM. It is
    the one thing in that section that is about the slots TOGETHER, so it has
    nowhere to live inside a face that shows one slot at a time.

    Twelve knobs, built from four catalog rows applied to three different
    nodes. That asymmetry is why this does not use OscillatorSection's own
    attachKnob: that one always writes the SELECTED slot, and every cell here
    writes a fixed one.

    The defaults are the identity: every amount at zero and every output at one
    is three oscillators summed in parallel, which is what the synth was before
    there was a matrix. Nothing here has to explain that to the engine - see
    snapshotRead::anyFmIn.
*/
class FmMatrixPanel : public juce::Component, private juce::ValueTree::Listener
{
public:
    explicit FmMatrixPanel (ProjectDocument&);
    ~FmMatrixPanel() override;

    /** Hands every cell what a right-click menu needs. Null means no menus. */
    void setParamMenuHost (const paramMenu::Host*);

    /** Points the matrix at a channel's INSTRUMENT node. An invalid tree
        disables every cell rather than leaving the last channel's routing on
        screen. */
    void setOwner (juce::ValueTree instrument);

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** How tall the matrix needs to be. A constant, like the faces beside it:
        the host has to budget before anything is laid out, and the grid is
        three rows whatever is in them. */
    static constexpr int preferredHeight = 3 * tokens::size::knobRow // one row per slot
                                           + 2 * tokens::space::sm;  // between the rows

    // --- for tests -----------------------------------------------------------

    /** The cell at [source slot][column], where column 3 is the output. */
    DewKnob& cellAt (int row, int column) const;

    static constexpr int numColumns = 4;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    juce::ValueTree slotAt (int index) const;

    /** Which catalog row each column is. The DESTINATION columns are named one
        per slot in the catalog, so this is the join between a column index and
        the property it writes - and a static_assert in ModuleCatalog holds the
        two counts equal. */
    static const juce::Identifier& propertyForColumn (int column) noexcept;

    ProjectDocument& document;
    juce::ValueTree instrument;

    /** Row-major, [row * numColumns + column]. Owned rather than declared as
        twelve members, because eleven of them would be the same declaration
        with a different index in it. */
    std::vector<std::unique_ptr<DewKnob>> cells;

    /** Where resized() put the row labels, so paint() draws them without
        repeating the arithmetic.

        There is deliberately no matching row of column headings. Every knob
        carries the catalog's own caption - TO 1, TO 2, TO 3, OUT - on every
        row, so a heading strip above them said each column's name a second
        time and spent a row of the narrowest panel in the application doing it.
    */
    std::vector<juce::Rectangle<int>> rowLabelBounds;

    bool updating = false;

    /** One gesture for the whole matrix: only one cell can be dragged. */
    RotaryGesture gesture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmMatrixPanel)
};

} // namespace dew
