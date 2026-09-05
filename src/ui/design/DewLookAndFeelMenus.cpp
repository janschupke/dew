// DewLookAndFeel's dropdowns and menus.
//
// The same class, a second translation unit - the shape PlaylistMenus.cpp
// already uses for PlaylistComponent. These belong together and to nothing
// else: a dropdown's closed box, the list it opens and every popup menu in the
// application are drawn by the six overrides below, and they share the glyph
// column and the two-line row that the rest of the look and feel knows nothing
// about.

#include "ui/design/DewLookAndFeel.h"

#include "ui/design/Animator.h"
#include "ui/design/Focus.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Tokens.h"

namespace dew
{

// --- combo boxes -------------------------------------------------------------

namespace
{

juce::Rectangle<float> glyphSquare (juce::Rectangle<int> column)
{
    return column.toFloat().withSizeKeepingCentre ((float) tokens::size::glyphMark,
                                                   (float) tokens::size::glyphMark);
}

} // namespace

void DewLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown, int,
                                   int, int, int, juce::ComboBox& box)
{
    using namespace tokens;

    const auto
        bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (stroke::whisper);
    const auto over = box.isMouseOver (true);

    // Painted like DewButton, because that is what it is standing next to.
    g.setColour (! box.isEnabled() ? colour::surface
                 : isButtonDown    ? colour::surfaceHover
                 : over            ? colour::surfaceHover.withAlpha (emphasis::strong)
                                   : colour::surfaceRaised);
    g.fillRoundedRectangle (bounds, radius::md);

    // A dropdown says focus with its border where the hand-painted primitives
    // add a ring, and the two are the same statement - so the border answers
    // the same question about whether it should be MADE. See focus::ringVisible.
    g.setColour (focus::ringVisibleFor (box.hasKeyboardFocus (false)) ? colour::accent
                 : over ? colour::outline.brighter (emphasis::controlLift)
                        : colour::outline);
    g.drawRoundedRectangle (bounds, radius::md, stroke::hairline);

    // The app's own chevron rather than JUCE's triangle.
    const auto chevron = juce::Rectangle<float> (bounds.getRight() - 26.0f,
                                                 bounds.getCentreY() - 8.0f, 16.0f, 16.0f);

    const auto foreground = box.isEnabled() ? colour::textSecondary : colour::textDisabled;
    icons::draw (g, icons::chevronDown(), chevron, foreground);

    // The selected option's own glyph, so the closed box shows what the list
    // showed. Read from the selection at PAINT time rather than from the inset
    // positionComboBoxText decided at layout time, because the selection moves
    // without a layout.
    if (const auto glyph = selectedGlyph (box); ! glyph.isEmpty())
    {
        const auto gutter = juce::Rectangle<int> (space::lg, 0, size::glyphColumn, height);
        icons::draw (g, glyph, glyphSquare (gutter),
                     box.isEnabled() ? colour::textPrimary : colour::textDisabled);
    }
}

void DewLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Room on the right for the chevron, and the same inset a DewButton uses -
    // plus the glyph column when this box has options that carry one.
    const auto left = tokens::space::lg
                      + (hasGlyphs (box) ? tokens::size::glyphColumn + tokens::space::xs : 0);

    label.setBounds (left, 0, juce::jmax (0, box.getWidth() - left - 30), box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId,
                     box.isEnabled() ? tokens::colour::textPrimary : tokens::colour::textDisabled);
}

juce::Font DewLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return tokens::type::font (tokens::type::body);
}

// --- menus -------------------------------------------------------------------

juce::PopupMenu::Options DewLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box,
                                                                         juce::Label& label)
{
    using namespace tokens;

    // This is LookAndFeel_V2's set minus ONE option. PopupMenu first places the
    // window correctly, flush under the box; then, if withItemThatMustBeVisible
    // is set, ensureItemComponentIsVisible drags the whole window back up until
    // the ticked row lands on the box. That single option was the entire reason
    // a dropdown opened over its own select.
    //
    // withInitiallySelectedItem stays, so arrowing through the menu still starts
    // from the current value - what was wrong was the placement, not the focus.
    //
    // Order matters: withTargetComponent overwrites targetArea, so the explicit
    // area has to come after it. The area is EXPANDED downwards rather than
    // moved, because calculateWindowPos takes y = target.getBottom() - that is
    // what leaves a small gap under the box instead of butting against it.
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (&box)
                       .withTargetScreenArea (
                           box.getScreenBounds().withHeight (box.getHeight() + space::xxs))
                       .withInitiallySelectedItem (box.getSelectedId())
                       .withMinimumWidth (box.getWidth())
                       .withMaximumNumColumns (1)
                       .withStandardItemHeight (label.getHeight());

    // Inside a dialog, the menu is drawn INTO the dialog rather than as a
    // window of its own.
    //
    // Every dew dialog is a DialogWindow with useNativeTitleBar set, so it is a
    // real NSWindow with real system buttons. A PopupMenu on the desktop is
    // another window, and opening one takes key status away from the dialog -
    // at which point macOS greys out its close and zoom buttons and stops them
    // answering. Nothing in dew was hiding them; the dialog had simply stopped
    // being the active window because its own dropdown was open.
    //
    // Only dialogs. The main window's boxes - the pattern selector, the roll's
    // snap grid - keep a desktop menu, which is free to overflow the window
    // they sit in; a dialog is small enough that its own bounds are no worse,
    // and JUCE scrolls a list too long to fit.
    if (auto* topLevel = box.getTopLevelComponent();
        dynamic_cast<juce::DialogWindow*> (topLevel) != nullptr)
        options = options.withParentComponent (topLevel);

    return options;
}

void DewLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    using namespace tokens;

    const auto
        bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (stroke::whisper);

    g.setColour (colour::surface);
    g.fillRoundedRectangle (bounds, radius::md);

    g.setColour (colour::outline);
    g.drawRoundedRectangle (bounds, radius::md, stroke::hairline);
}

void DewLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon, const juce::Colour*)
{
    using namespace tokens;

    if (isSeparator)
    {
        g.setColour (colour::divider);
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + space::md,
                              (float) area.getRight() - space::md);
        return;
    }

    auto row = area.reduced (space::xs, space::xxs);

    if (isHighlighted && isActive)
    {
        g.setColour (colour::accent);
        g.fillRoundedRectangle (row.toFloat(), radius::sm);
    }

    const auto textColour = ! isActive      ? colour::textDisabled
                            : isHighlighted ? colour::textOnAccent
                                            : colour::textPrimary;

    auto content = row.reduced (space::md, 0);

    // The tick gutter is always reserved, ticked or not: a menu where some rows
    // are indented and others are not reads as misaligned.
    const auto tickArea = content.removeFromLeft (16);
    content.removeFromLeft (space::xs);

    if (isTicked)
        icons::draw (g, icons::check(), tickArea.toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                     textColour);

    // The row's own glyph, in its own column. NOT in the tick gutter: an
    // automation segment's shape is ticked AND pictured at once, and a single
    // column would have made the two states of that menu unreadable.
    //
    // Spent only when there is one to spend, while getIdealPopupMenuItemSize
    // below always measures it - so a menu with no glyphs is not indented past
    // a column of nothing, and a menu with them cannot be too narrow for it.
    if (icon != nullptr)
    {
        const auto glyphArea = content.removeFromLeft (size::glyphColumn);
        content.removeFromLeft (space::xs);

        // Painted in the row's OWN colour rather than the drawable's. A
        // DrawablePath bakes a fill in when it is built, which is before the
        // theme and the highlight are known; taking the colour here is what
        // makes a glyph legible on the accent fill and what keeps it inside the
        // contrast already proven for this row's text.
        if (const auto* carried = dynamic_cast<const MenuGlyph*> (icon))
            icons::draw (g, carried->glyph(), glyphSquare (glyphArea), textColour);
        else
            icon->drawWithin (g, glyphSquare (glyphArea), juce::RectanglePlacement::centred, 1.0f);
    }

    if (hasSubMenu)
    {
        const auto arrow = content.removeFromRight (16).toFloat().withSizeKeepingCentre (12.0f,
                                                                                         12.0f);
        icons::draw (g, icons::chevronRight(), arrow, textColour);
    }

    if (shortcutKeyText.isNotEmpty())
    {
        // Dimmed only where there is room to be: on the HIGHLIGHTED row the
        // text is textOnAccent over the accent fill, and taking it to dimmed
        // there drops it to 2.9:1 - a shortcut is text you read, not a texture.
        g.setColour (isHighlighted ? textColour : textColour.withAlpha (emphasis::dimmed));
        g.setFont (type::font (type::small));
        g.drawText (shortcutKeyText, content.removeFromRight (72),
                    juce::Justification::centredRight, false);
    }

    g.setColour (textColour);
    g.setFont (type::font (type::body));
    g.drawText (text, content, juce::Justification::centredLeft, true);
}

void DewLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                int standardMenuItemHeight, int& idealWidth,
                                                int& idealHeight)
{
    using namespace tokens;

    if (isSeparator)
    {
        idealWidth = 60;
        idealHeight = space::md;
        return;
    }

    // The glyph column is measured whether this row has one or not. The size
    // hook is not told about the icon - only the draw is - so the choice is
    // between a menu that is always wide enough and one that clips its longest
    // label whenever it turns out to be glyphed. Width is a maximum over rows,
    // so the slack costs an unglyphed menu a little air and nothing else.
    constexpr auto glyphSpan = size::glyphColumn + space::xs;

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : size::controlHeight;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (type::font (type::body), text)
                 + space::xxl * 2 + glyphSpan;
}

void DewLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                 const juce::Rectangle<int>& area,
                                                 const juce::String& sectionName)
{
    using namespace tokens;

    // Indented to the row's own edge, and no further: a row then spends the
    // tick gutter and the glyph column before its label, so a heading hangs
    // OUTBOARD of the names it gathers. That is the hierarchy it is there to
    // show, and it is why this does not reach for size::glyphColumn - a heading
    // has neither a tick nor a glyph, and lining it up with the labels would
    // make it look like one more row that had lost its picture.
    const auto row = area.reduced (space::xs, 0).reduced (space::md, 0);

    // A label rather than a row: small, dimmed and not on the accent, because a
    // header is never highlighted and never chosen. JUCE gives it no tick
    // gutter and no glyph column, which is what makes it read as a heading of
    // the rows rather than as one of them.
    g.setColour (colour::textSecondary.withAlpha (emphasis::strong));
    g.setFont (type::font (type::small));
    g.drawText (sectionName, row, juce::Justification::centredLeft, true);
}

void DewLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int,
                                                                    int& idealWidth,
                                                                    int& idealHeight,
                                                                    const juce::PopupMenu::Options&)
{
    using namespace tokens;

    // Its own height rather than JUCE's default, which is an item and half
    // again - a heading a row and a half tall separates the groups it is meant
    // to gather. The air above it is what does that work instead.
    idealHeight = juce::roundToInt (type::small) + space::md + space::xs;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (type::font (type::small), text)
                 + space::xxl * 2;
}

int DewLookAndFeel::getPopupMenuBorderSize()
{
    return tokens::space::xs;
}

void DewLookAndFeel::preparePopupMenuWindow (juce::Component& window)
{
    // Reduce motion has to reach the one animation that predates the animator.
    // This uses the desktop animator rather than dew's, because JUCE owns a
    // menu window's lifetime and the desktop's is the only one still alive when
    // that window is deleted out from under us.
    if (Animator::shared().getReduceMotion())
        return;

    // A menu that simply appears reads as a redraw; a short fade and lift reads
    // as something opening. JUCE offers no hook for the close, so this is
    // deliberately one-directional rather than half an animation.
    const auto target = window.getBounds();

    window.setAlpha (0.0f);
    window.setBounds (target.translated (0, tokens::motion::popupRisePx));

    juce::Desktop::getInstance().getAnimator().animateComponent (
        &window, target, 1.0f, tokens::motion::popupMs, false, 1.0, 0.0);
}

} // namespace dew
