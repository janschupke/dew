#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace dew
{

/** A menu row's leading glyph, in transit.

    JUCE gives a PopupMenu::Item one place to put a picture - a Drawable - and
    DewLookAndFeel threw it away, so every menu and every dropdown in dew was
    text. This is what travels in that slot.

    It carries the PATH and nothing else, deliberately. A DrawablePath bakes its
    fill colour in, and a row's colour is not knowable when the menu is built:
    it depends on the theme and on whether the row is the highlighted one. So
    the look and feel unwraps this and paints the path itself, in the same
    colour it has already decided to give the label.

    The alternative was the trick menuRow uses for a second line - smuggle the
    icon's name inside the item's text and split it back apart when drawing -
    and it was rejected. A sentinel in the text leaks out through
    ComboBox::getText, through the accessible name, and through dew::menuItems,
    which is the seam every menu test in the repository reads.
*/
class MenuGlyph final : public juce::DrawablePath
{
public:
    explicit MenuGlyph (juce::Path pathInUnitSquare);

    /** The path as icons:: produced it: in a 0..1 square, with no colour. */
    const juce::Path& glyph() const noexcept
    {
        return unitPath;
    }

    std::unique_ptr<juce::Drawable> createCopy() const override;

private:
    juce::Path unitPath;
};

/** Adds one glyphed row to a menu.

    Every glyphed item goes through this, which is the point: a row added with a
    plain addItem is visibly a row with no glyph, at the call site, rather than
    one that quietly lost its picture. A menu is either fully glyphed or not
    glyphed at all - the same rule the tick gutter already follows, for the same
    reason, which is that a menu where some rows are indented and others are not
    reads as misaligned.
*/
void addGlyphItem (juce::PopupMenu& menu, int itemId, const juce::String& text, juce::Path glyph,
                   bool isEnabled = true, bool isTicked = false);

/** Adds a glyphed row that opens a submenu. */
void addGlyphSubMenu (juce::PopupMenu& menu, const juce::String& text, juce::PopupMenu subMenu,
                      juce::Path glyph, bool isEnabled = true);

/** The same, for a dropdown's option list.

    Goes through ComboBox::getRootMenu rather than addItem because addItem takes
    a text and an id and offers nowhere to put a picture. That is the documented
    extension point, and ComboBox::addItem is itself a one-line forward to the
    same menu, so getNumItems, getItemText and setSelectedId - which all walk
    that menu - keep working.
*/
void addGlyphItem (juce::ComboBox& box, int itemId, const juce::String& text, juce::Path glyph);

/** The glyph on the option `box` currently has selected, or an empty path.

    Read back out of the root menu rather than kept alongside it: ComboBox::clear
    is not virtual, so a list held next to the menu would survive a clear the
    menu did not, and the box would go on drawing an option that no longer
    exists.
*/
juce::Path selectedGlyph (const juce::ComboBox& box);

/** Whether any of `box`'s options carries a glyph.

    The CLOSED box's text inset is decided from this rather than from the
    selected option, because it is asked for on layout and the selection moves
    without one. An inset that followed the selection would be right when it was
    computed and wrong by the time it was used.
*/
bool hasGlyphs (const juce::ComboBox& box);

} // namespace dew
