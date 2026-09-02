#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** What a PopupMenu offers, as text a test can read.

    showMenuAsync cannot be driven headlessly, so every menu in dew is built by
    a named method and applied by another, and the test reads the built menu
    rather than the window. Three places wrote this loop out, each with the same
    comment attached, because getting it wrong is silent:

        MenuItemIterator keeps a REFERENCE to the menu it was given, so
        iterating a TEMPORARY walks a destroyed object and yields nothing.

    Taking a `const&` is what makes that structural rather than a comment - the
    caller has to have a named menu to pass one in.

    A separator comes back as "-", so a test can assert where the groups are and
    not only what is in them.
*/
inline juce::StringArray menuItems (const juce::PopupMenu& menu)
{
    juce::StringArray items;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().isSeparator ? "-" : it.getItem().text);

    return items;
}

} // namespace dew
