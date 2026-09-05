#include "ui/PresetMenu.h"

#include "model/PresetLibrary.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** How many presets each category holds, in the order the categories are
    declared. Counting first is what lets the decision about headings be made
    once, before any row is built, rather than guessed per row. */
std::vector<int> countsByCategory (const std::vector<Preset>& presets)
{
    std::vector<int> counts (presetCategories().size(), 0);

    for (const auto& preset : presets)
        if (preset.category.has_value())
            ++counts[(size_t) *preset.category];

    return counts;
}

bool shouldShowHeaders (const std::vector<int>& counts, size_t numUncategorised)
{
    int used = 0;
    bool anyRun = false;

    for (const auto count : counts)
    {
        if (count > 0)
            ++used;

        if (count > 1)
            anyRun = true;
    }

    // An uncategorised group is a group: a menu with one category and a
    // leftover is two runs, and leaving it unlabelled would put those rows
    // under the wrong heading.
    if (numUncategorised > 0)
        ++used;

    return used > 1 && anyRun;
}

} // namespace

std::vector<PresetMenuRow> presetMenuRows (const std::vector<Preset>& presets)
{
    const auto counts = countsByCategory (presets);

    size_t numUncategorised = 0;

    for (const auto& preset : presets)
        if (! preset.category.has_value())
            ++numUncategorised;

    const auto headers = shouldShowHeaders (counts, numUncategorised);

    std::vector<PresetMenuRow> rows;

    const auto append = [&] (const std::optional<PresetCategory>& category)
    {
        bool wroteHeader = false;

        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (presets[i].category != category)
                continue;

            if (headers && ! wroteHeader && category.has_value())
            {
                rows.push_back ({ true, presetCategoryDisplayName (*category), {}, -1 });
                wroteHeader = true;
            }

            rows.push_back ({ false, PresetLibrary::displayName (presets[i]),
                              PresetLibrary::describe (presets[i]), (int) i });
        }
    };

    for (const auto& descriptor : presetCategories())
        append (descriptor.category);

    // Last, and never under a heading of their own: a preset whose category
    // this build does not know is offered rather than hidden, but inventing a
    // name for where it belongs would be worse than saying nothing.
    append ({});

    return rows;
}

PresetMenuItem::PresetMenuItem (juce::String labelIn, juce::String descriptionIn,
                                juce::Path glyphIn,
                                std::function<void (const juce::String&)> onHoverIn)
    : juce::PopupMenu::CustomComponent (true)
    , label (std::move (labelIn))
    , description (std::move (descriptionIn))
    , glyph (std::make_unique<MenuGlyph> (std::move (glyphIn)))
    , onHover (std::move (onHoverIn))
{
    // Both surfaces read this one string: the floating window asks this
    // component for it, and the status strip is told it on the way in.
    setTooltip (description);
}

void PresetMenuItem::paint (juce::Graphics& g)
{
    const auto* item = getItem();

    // Asked, not answered: every colour, the highlight fill, the tick gutter
    // and the glyph column stay the look and feel's to decide, so this row
    // cannot drift from every other row in the application.
    getLookAndFeel().drawPopupMenuItem (
        g, getLocalBounds(), false, item == nullptr || item->isEnabled, isItemHighlighted(),
        item != nullptr && item->isTicked, false, label, {}, glyph.get(), nullptr);
}

void PresetMenuItem::getIdealSize (int& idealWidth, int& idealHeight)
{
    // 0 for the standard height, which is what a menu that is not a dropdown's
    // list passes anyway - so a preset row is exactly as tall as the rows in
    // every other menu, and no longer twice that.
    getLookAndFeel().getIdealPopupMenuItemSize (label, false, 0, idealWidth, idealHeight);
}

void PresetMenuItem::mouseEnter (const juce::MouseEvent&)
{
    if (onHover != nullptr)
        onHover (description);
}

void PresetMenuItem::mouseExit (const juce::MouseEvent&)
{
    if (onHover != nullptr)
        onHover ({});
}

void addPresetRows (juce::PopupMenu& menu, const std::vector<PresetMenuRow>& rows,
                    const std::function<void (const juce::String&)>& onHover)
{
    for (const auto& row : rows)
    {
        if (row.isHeader)
        {
            menu.addSectionHeader (row.label);
            continue;
        }

        juce::PopupMenu::Item item (row.label);

        // The index the preset had before it was grouped. A menu that numbered
        // its rows would load the wrong sound the moment a heading moved one.
        item.itemID = row.presetIndex + 1;
        item.customComponent = new PresetMenuItem (
            row.label, row.description, glyph::forAction (glyph::Action::preset), onHover);

        // The text as well as the component. JUCE does not need it to draw the
        // row, but dew::menuItems and the accessible name both read it, and a
        // row that told them nothing would be invisible to every menu test in
        // the repository.
        menu.addItem (std::move (item));
    }
}

} // namespace dew
