#pragma once

#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Glyphs.h"

namespace dew
{

/** A menu row's leading glyph, in transit.

    JUCE gives a PopupMenu::Item one place to put a picture - a Drawable - and
    DewLookAndFeel threw it away, so every menu and every dropdown in dew was
    text. This is what travels in that slot.

    It carries the PATH and, where the action has one, a SEMANTIC TINT. Not a
    baked fill: a DrawablePath bakes its colour in, and a row's colour is not
    knowable when the menu is built - it depends on the theme, on whether the
    row is the highlighted one, and on whether it is enabled at all. So the look
    and feel unwraps this and paints the path itself, choosing between the tint
    and the row's own colour once it knows those three things.

    The tint is what the ACTION means (glyph::tintFor), carried rather than
    applied, for the same reason the path is.

    The alternative was to smuggle the icon's name inside the item's text and
    split it back apart when drawing, and it was rejected. A sentinel in the
    text leaks out through ComboBox::getText, through the accessible name, and
    through dew::menuItems, which is the seam every menu test in the repository
    reads. A row that needs more than a label is a PopupMenu::CustomComponent -
    see PresetMenuItem, which is what finally retired the second line.
*/
class MenuGlyph final : public juce::DrawablePath
{
public:
    explicit MenuGlyph (juce::Path pathInUnitSquare, std::optional<juce::Colour> semanticTint = {});

    /** The path as icons:: produced it: in a 0..1 square, with no colour. */
    const juce::Path& glyph() const noexcept
    {
        return unitPath;
    }

    /** What this row's action means, as a colour, or nothing for the actions
        that mean nothing in particular. The look and feel decides whether it
        can be honoured - a highlighted row is accent-filled, and a semantic
        hue on that fill would be the one pair the contrast tables never cover.
    */
    const std::optional<juce::Colour>& tint() const noexcept
    {
        return semantic;
    }

    std::unique_ptr<juce::Drawable> createCopy() const override;

private:
    juce::Path unitPath;
    std::optional<juce::Colour> semantic;
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

/** The same three adders, named by the ACTION rather than by its picture.

    Prefer these. Naming the action is what carries the semantic tint with it,
    so a destructive row cannot be added in the colour of an ordinary one - which
    is precisely how every trash can in the application came to be white while
    every trash BUTTON was red. Passing a bare Path still works and still means
    "this shape, no meaning", which is what an effect's or an instrument's own
    icon is.
*/
void addGlyphItem (juce::PopupMenu& menu, int itemId, const juce::String& text,
                   glyph::Action action, bool isEnabled = true, bool isTicked = false);

void addGlyphSubMenu (juce::PopupMenu& menu, const juce::String& text, juce::PopupMenu subMenu,
                      glyph::Action action, bool isEnabled = true);

void addGlyphItem (juce::ComboBox& box, int itemId, const juce::String& text, glyph::Action action);

/** Whether any of `box`'s options carries a glyph.

    The CLOSED box's text inset is decided from this rather than from the
    selected option, because it is asked for on layout and the selection moves
    without one. An inset that followed the selection would be right when it was
    computed and wrong by the time it was used.
*/
bool hasGlyphs (const juce::ComboBox& box);

} // namespace dew
