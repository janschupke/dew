#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/EntityColour.h"
#include "model/ProjectDocument.h"
#include "model/ProjectEdits.h"

namespace dew::colourMenu
{

/** The "Colour" submenu, once, for the three things that have a colour.

    A channel row, a playlist track header and a mixer strip all offer it, and
    all three would otherwise spell out the same nine items, the same id
    arithmetic and the same undo call. The vocabulary is the ramp's, so a colour
    a project can hold is a colour the menu can set and there is no fourth list.

    Ids are handed in as a base rather than declared here: each menu already
    numbers its own items, and a submenu that picked its own range would be a
    second numbering free to collide with the first.
*/

/** How many ids `build` uses, starting at the base: one per ramp entry plus
    "Default". */
inline int itemCount()
{
    return entityColour::rampSize() + 1;
}

/** The item that clears the colour. Last, after a separator: it is the odd one
    out, and the eight above it are the answer most of the time. */
inline int defaultItemFor (int baseId)
{
    return baseId + entityColour::rampSize();
}

/** Adds "Colour" to `menu` as a submenu, ticking the one this node is on.

    `node` may be a channel, a playlist track or a mixer strip - the property is
    the same on all three, which is why ProjectEdits::setColour is one function.
*/
inline void addTo (juce::PopupMenu& menu, const juce::ValueTree& node, int baseId)
{
    juce::PopupMenu colours;

    const auto chosen = entityColour::stored (node);

    for (int i = 0; i < entityColour::rampSize(); ++i)
    {
        const auto swatch = juce::Colour::fromString (entityColour::defaultHex (i));

        colours.addItem (baseId + i, entityColour::rampName (i), true,
                         chosen.has_value() && *chosen == swatch);
    }

    colours.addSeparator();

    // Ticked when nothing is stored, so "inherit" is a state the menu SHOWS
    // rather than one you can only infer from nothing else being ticked.
    colours.addItem (defaultItemFor (baseId), "Default", true, ! chosen.has_value());

    menu.addSubMenu ("Colour", colours);
}

/** Applies a choice if it belongs to this submenu, and says whether it did.

    Returning a bool rather than throwing the caller's switch away: the choice
    arrives as one integer and the caller has its own items in the same space,
    so it asks this first and falls through to its own handling.
*/
inline bool apply (int choice, juce::ValueTree node, int baseId, ProjectDocument& document)
{
    if (choice < baseId || choice >= baseId + itemCount())
        return false;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Change colour");

    const auto index = choice - baseId;

    ProjectEdits::setColour (node,
                             index < entityColour::rampSize()
                                 ? entityColour::defaultHex (index)
                                 : juce::String(),
                             &undo);

    return true;
}

} // namespace dew::colourMenu
