#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Preset.h"

namespace dew
{

/** One row of a preset picker: a category heading, or a preset.

    The seam both pickers are read through. It replaced a juce::StringArray in
    which the description was a "\n" suffix and the preset's index was the row's
    position - two facts encoded in a shape that could not carry them, which is
    the same trick MenuGlyph was written to stop using for icons. A header has
    no index and a preset's index survives regrouping, so neither is inferred
    from where the row happens to sit.
*/
struct PresetMenuRow
{
    bool isHeader = false;

    /** The category's name, or the preset's - already in the active locale. */
    juce::String label;

    /** The sentence under the name. Empty for a header. Shown on HOVER now,
        not painted into the row: see PresetMenuItem. */
    juce::String description;

    /** Which preset this row loads, as an index into the vector the rows were
        built from. -1 for a header. */
    int presetIndex = -1;
};

/** `presets` grouped for display, in the order presetCategories() declares.

    Headers are emitted only when the menu has two or more categories AND at
    least one of them holds two or more presets. A heading earns its place by
    gathering a RUN of rows; one standing over a single row is noise, and with
    three presets to an effect type every menu would otherwise be half headings.
    So the grouping appears where it helps - the synth's five sounds today - and
    stays out of the way until a type has enough presets to need it.

    Stable within a category, so the factory's hand-ordering survives.
*/
std::vector<PresetMenuRow> presetMenuRows (const std::vector<Preset>& presets);

/** A preset's row, which says what the preset is FOR when you hover it.

    A PopupMenu::Item carries a text and a shortcut, so dew's first answer to
    "where does the description go" was to paint it into the row as a second
    line. That doubles every row's height and turns a picker into prose you read
    to find the one name you were looking for.

    A tooltip is the surface that answers "what is this?" without being in the
    way of "which one?", and an item can have one after all - JUCE's tooltip
    window reads the component under the pointer, and a CustomComponent is one.
    What it costs is that this row now draws itself; what stops that becoming a
    second look and feel is that it draws itself by ASKING the look and feel,
    so the highlight, the tick gutter, the glyph column and every colour are
    still decided in exactly one place.

    TooltipWindow::getTipFor does not walk up the parent chain the way
    HoverHelp::helpFor does, so the tooltip has to be on this component rather
    than on anything containing it.
*/
class PresetMenuItem final : public juce::PopupMenu::CustomComponent,
                             public juce::SettableTooltipClient
{
public:
    /** `onHover` is told the description as the pointer arrives and an empty
        string as it leaves, so the status strip answers at once where the
        floating tooltip waits - the two surfaces HoverHelp already gives every
        other control in the application. */
    PresetMenuItem (juce::String label, juce::String description, juce::Path glyph,
                    std::function<void (const juce::String&)> onHover);

    void paint (juce::Graphics&) override;
    void getIdealSize (int& idealWidth, int& idealHeight) override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String label;
    juce::String description;
    /** Built once and kept: a Drawable is a Component, and one made fresh on
        every repaint is a Component constructed inside paint(). */
    std::unique_ptr<juce::Drawable> glyph;
    std::function<void (const juce::String&)> onHover;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetMenuItem)
};

/** Builds `menu` from `rows`, giving every preset row the id applyPresetChoice
    expects - its index plus one, never its position in the menu. */
void addPresetRows (juce::PopupMenu& menu, const std::vector<PresetMenuRow>& rows,
                    const std::function<void (const juce::String&)>& onHover);

} // namespace dew
