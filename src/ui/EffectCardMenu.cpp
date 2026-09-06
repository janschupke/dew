#include "ui/MenuSeam.h"
#include "ui/EffectCard.h"

#include "i18n/Strings.h"
#include "model/PresetLibrary.h"
#include "model/ProjectEdits.h"
#include "ui/EffectChainComponent.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"

/*  The effect card's context menu.

    Its own translation unit for the reason PlaylistMenus.cpp is: a menu is a
    list of offers and the arithmetic of a card is not, and EffectCard.cpp was
    at 397 of the 400 code lines the tree holds itself to before this menu
    existed at all.

    The menu is also what the header stopped being. Move-up and move-down were
    two more chevrons in a 34-pixel row, beside a collapse caret drawn from the
    same two glyphs - so an open card showed chevron-up twice and a closed one
    chevron-down twice, each pair meaning two different things. Reorder is a
    grip to drag and a row with a WORD on it here; the header keeps one chevron,
    and it means what a chevron means.
*/

namespace dew
{

juce::PopupMenu EffectCard::buildMenu() const
{
    juce::PopupMenu menu;

    // Reorder first, because it is what left the header. Disabled rather than
    // absent at the ends of the chain: a row that comes and goes is a row a
    // person has to look for, and "greyed out" says "not here" where "missing"
    // says "not anywhere".
    addGlyphItem (menu, (int) MenuItem::moveUp, tr (StringId::effect_moveUp_label),
                  glyph::Action::moveUp, index > 0);
    addGlyphItem (menu, (int) MenuItem::moveDown, tr (StringId::effect_moveDown_label),
                  glyph::Action::moveDown, index + 1 < owner.getNumSlotRows());

    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::preset, tr (StringId::effect_preset_label),
                  glyph::Action::preset, ! PresetLibrary::presetsFor (type).empty());

    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::remove, tr (StringId::effect_remove_label),
                  glyph::Action::remove);

    return menu;
}

void EffectCard::applyMenuChoice (int choice)
{
    switch ((MenuItem) choice)
    {
        case MenuItem::moveUp: owner.moveSlot (index, index - 1); return;
        case MenuItem::moveDown: owner.moveSlot (index, index + 1); return;
        case MenuItem::preset: owner.showPresetMenu (index, *this); return;

        case MenuItem::remove:
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Remove effect");
            ProjectEdits::removeEffect (owner.getOwner(), effect, &undo);
            return;
        }
    }
}

void EffectCard::showMenu (const juce::MouseEvent& event)
{
    // The menu is the CARD's. forwardChildMouseEventsTo brings every child's
    // press here as well, so without this a right-click on a knob opened that
    // knob's parameter menu and this one on top of it - the defect isOwnPress
    // exists for, already found once on the mixer strip and the rack row.
    if (! isOwnPress (event, *this))
        return;

    auto menu = buildMenu();

    showMenuAt<EffectCard> (menu, *this, event,
                            [] (EffectCard& card, int choice) { card.applyMenuChoice (choice); });
}

} // namespace dew
