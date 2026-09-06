#pragma once

#include <functional>

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

/** The other half of the same seam: showing one.

    Five places wrote this out - the mixer strip, the instrument panel, the
    effect card, the rack row and the playlist - and every one of them had to
    remember the same three things:

      - `setLookAndFeel (&getLookAndFeel())`, without which the menu is drawn
        by JUCE's default look rather than dew's. It is one line and it is
        invisible when missing until somebody opens the menu.
      - a target area one pixel square at the mouse, rather than the component,
        so the menu opens under the pointer and not under the corner of the row.
      - a SafePointer, because a menu outlives the press: a rack row can be
        removed by the very menu it opened, and a raw `this` in that callback is
        a use-after-free that only shows up when somebody deletes a channel.

    The callback takes the choice only when it is non-zero. Zero is what
    dismissing a menu returns, and every one of the five checked for it - which
    is worth having once rather than five times, because forgetting it applies
    item 0 to a menu somebody closed.
*/
template <typename Owner>
void showMenuAt (juce::PopupMenu& menu, Owner& owner, const juce::MouseEvent& event,
                 std::function<void (Owner&, int choice)> chosen)
{
    menu.setLookAndFeel (&owner.getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                            { event.getScreenX(), event.getScreenY(), 1, 1 }),
                        [safe = juce::Component::SafePointer<Owner> (&owner),
                         apply = std::move (chosen)] (int choice)
                        {
                            if (safe != nullptr && choice > 0)
                                apply (*safe, choice);
                        });
}

} // namespace dew
