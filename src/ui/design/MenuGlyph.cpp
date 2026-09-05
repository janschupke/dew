#include "ui/design/MenuGlyph.h"

#include <utility>

namespace dew
{

MenuGlyph::MenuGlyph (juce::Path pathInUnitSquare)
    : unitPath (std::move (pathInUnitSquare))
{
    // The base class is given the path too. Nothing in dew draws a MenuGlyph
    // through the Drawable interface, but a Drawable that measured zero would
    // be one JUCE could legitimately skip before the look and feel ever sees
    // it, and a glyph that vanishes for a reason that far away is not worth
    // saving a copy over.
    setPath (unitPath);
}

std::unique_ptr<juce::Drawable> MenuGlyph::createCopy() const
{
    return std::make_unique<MenuGlyph> (unitPath);
}

namespace
{

std::unique_ptr<juce::Drawable> carrierFor (juce::Path glyph)
{
    return std::make_unique<MenuGlyph> (std::move (glyph));
}

} // namespace

void addGlyphItem (juce::PopupMenu& menu, int itemId, const juce::String& text, juce::Path glyph,
                   bool isEnabled, bool isTicked)
{
    menu.addItem (juce::PopupMenu::Item (text)
                      .setID (itemId)
                      .setEnabled (isEnabled)
                      .setTicked (isTicked)
                      .setImage (carrierFor (std::move (glyph))));
}

void addGlyphSubMenu (juce::PopupMenu& menu, const juce::String& text, juce::PopupMenu subMenu,
                      juce::Path glyph, bool isEnabled)
{
    juce::PopupMenu::Item item (text);
    item.subMenu = std::make_unique<juce::PopupMenu> (std::move (subMenu));
    item.isEnabled = isEnabled;
    item.image = carrierFor (std::move (glyph));
    menu.addItem (std::move (item));
}

void addGlyphItem (juce::ComboBox& box, int itemId, const juce::String& text, juce::Path glyph)
{
    // The two things ComboBox::addItem checks before forwarding, kept here
    // because going round it would otherwise mean going round them as well.
    jassert (text.isNotEmpty());
    jassert (itemId != 0);

    addGlyphItem (*box.getRootMenu(), itemId, text, std::move (glyph));
}

namespace
{

/** The glyph carried by the item with this id, or an empty path.

    `wantedId` of zero answers "does ANY option carry one", which is what the
    closed box's inset needs.
*/
juce::Path glyphOf (const juce::ComboBox& box, int wantedId)
{
    // A named local, not box.getRootMenu() inline: MenuItemIterator keeps a
    // REFERENCE to the menu it was given, so iterating a temporary walks a
    // destroyed object and yields nothing, silently.
    const auto* root = box.getRootMenu();

    if (root == nullptr)
        return {};

    for (juce::PopupMenu::MenuItemIterator it (*root); it.next();)
    {
        const auto& item = it.getItem();

        if (wantedId != 0 && item.itemID != wantedId)
            continue;

        if (const auto* carried = dynamic_cast<const MenuGlyph*> (item.image.get()))
            return carried->glyph();

        if (wantedId != 0)
            return {};
    }

    return {};
}

} // namespace

juce::Path selectedGlyph (const juce::ComboBox& box)
{
    const auto selected = box.getSelectedId();

    if (selected == 0)
        return {};

    return glyphOf (box, selected);
}

bool hasGlyphs (const juce::ComboBox& box)
{
    return ! glyphOf (box, 0).isEmpty();
}

} // namespace dew
