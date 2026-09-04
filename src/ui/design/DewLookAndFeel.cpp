#include "ui/design/DewLookAndFeel.h"

#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"

#include "ui/primitives/DewControls.h"
#include "ui/design/Animator.h"

namespace dew
{

DewLookAndFeel::DewLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, tokens::colour::background);
    setColour (juce::DocumentWindow::textColourId, tokens::colour::textPrimary);

    setColour (juce::Label::textColourId, tokens::colour::textPrimary);
    setColour (juce::TextEditor::backgroundColourId, tokens::colour::well);
    setColour (juce::TextEditor::textColourId, tokens::colour::textPrimary);
    setColour (juce::TextEditor::outlineColourId, tokens::colour::divider);
    setColour (juce::TextEditor::focusedOutlineColourId, tokens::colour::accent);

    setColour (juce::TextButton::buttonColourId, tokens::colour::surface);
    setColour (juce::TextButton::buttonOnColourId, tokens::colour::accent);
    setColour (juce::TextButton::textColourOffId, tokens::colour::textPrimary);
    setColour (juce::TextButton::textColourOnId, tokens::colour::background);

    setColour (juce::ComboBox::backgroundColourId, tokens::colour::surface);
    setColour (juce::ComboBox::textColourId, tokens::colour::textPrimary);
    setColour (juce::ComboBox::outlineColourId, tokens::colour::divider);
    setColour (juce::ComboBox::arrowColourId, tokens::colour::textSecondary);

    setColour (juce::PopupMenu::backgroundColourId, tokens::colour::surface);
    setColour (juce::PopupMenu::textColourId, tokens::colour::textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, tokens::colour::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, tokens::colour::background);

    setColour (juce::Slider::thumbColourId, tokens::colour::accent);
    setColour (juce::Slider::trackColourId, tokens::colour::accent);
    setColour (juce::Slider::backgroundColourId, tokens::colour::well);
    setColour (juce::Slider::textBoxTextColourId, tokens::colour::textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, tokens::colour::well);
    setColour (juce::Slider::textBoxOutlineColourId, tokens::colour::divider);

    setColour (juce::TabbedComponent::backgroundColourId, tokens::colour::background);
    setColour (juce::TabbedComponent::outlineColourId, tokens::colour::divider);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, tokens::colour::divider);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, tokens::colour::accent);
    setColour (juce::TabbedButtonBar::tabTextColourId, tokens::colour::textSecondary);
    setColour (juce::TabbedButtonBar::frontTextColourId, tokens::colour::textPrimary);

    setColour (juce::ScrollBar::thumbColourId, tokens::colour::dividerStrong);
    setColour (juce::ToggleButton::textColourId, tokens::colour::textPrimary);
    setColour (juce::ToggleButton::tickColourId, tokens::colour::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, tokens::colour::dividerStrong);

    setColour (juce::AlertWindow::backgroundColourId, tokens::colour::surface);
    setColour (juce::AlertWindow::textColourId, tokens::colour::textPrimary);
    setColour (juce::AlertWindow::outlineColourId, tokens::colour::divider);
}

juce::Font DewLookAndFeel::getLabelFont (juce::Label& label)
{
    // Deliberately the label's own font: setFont is how a caller opts a label
    // onto a specific token, and overriding that would break every one of them.
    // The labels that had NO font were the ones JUCE creates itself, which
    // createSliderTextBox below now covers.
    return label.getFont();
}

juce::Font DewLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    // V4 returns jmin (16, height * 0.6), which on the mixer's 18px M/S buttons
    // is 10.8 - a size that appears nowhere in the system, next to a
    // DewLetterToggle drawing the same glyph at 11.
    return tokens::type::font (tokens::type::body);
}

juce::Font DewLookAndFeel::getPopupMenuFont()
{
    return tokens::type::font (tokens::type::body);
}

juce::Font DewLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return tokens::type::font (tokens::type::small);
}

juce::Font DewLookAndFeel::getAlertWindowTitleFont()
{
    return tokens::type::font (tokens::type::title, true);
}

juce::Font DewLookAndFeel::getAlertWindowMessageFont()
{
    return tokens::type::font (tokens::type::body);
}

juce::Font DewLookAndFeel::getAlertWindowFont()
{
    return tokens::type::font (tokens::type::body);
}

namespace
{

constexpr float tooltipMaxWidth = 320.0f;

/** Tooltip text, laid out at a dew size. JUCE's own helper hard-codes 13pt bold
    and is in a detail namespace, so this is the only way onto the scale.
*/
juce::TextLayout layOutTooltip (const juce::String& text, juce::Colour textColour)
{
    juce::AttributedString attributed;
    attributed.setJustification (juce::Justification::centred);
    attributed.append (text, tokens::type::font (tokens::type::small), textColour);

    juce::TextLayout layout;
    layout.createLayout (attributed, tooltipMaxWidth);

    return layout;
}

} // namespace

juce::Rectangle<int> DewLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                       juce::Point<int> screenPos,
                                                       juce::Rectangle<int> parentArea)
{
    const auto layout = layOutTooltip (tipText, juce::Colours::black);

    const auto w = (int) (layout.getWidth() + 2.0f * tokens::space::md);
    const auto h = (int) (layout.getHeight() + 2.0f * tokens::space::xs);

    return juce::Rectangle<int> (
               screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w,
               h)
        .constrainedWithin (parentArea);
}

void DewLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width,
                                  int height)
{
    using namespace tokens;

    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (colour::surfaceRaised);
    g.fillRoundedRectangle (bounds, tokens::radius::sm);

    g.setColour (colour::outline);
    g.drawRoundedRectangle (bounds.reduced (stroke::whisper), tokens::radius::sm,
                            tokens::stroke::hairline);

    layOutTooltip (text, colour::textPrimary).draw (g, bounds);
}

juce::Label* DewLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    // V2 returns its own private SliderLabelComp, which cannot be constructed
    // here - so take that one and put a font on it. Without this the label keeps
    // juce::Label's untouched 15pt default, which is taller than the 15px and
    // 16px text boxes the instrument panel and mixer give it.
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    if (label != nullptr)
        label->setFont (tokens::type::font (tokens::type::small));

    return label;
}

void DewLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float, juce::Slider& slider)
{
    // Delegates to the same routine DewKnob uses, so a stock juce::Slider and a
    // dew primitive cannot end up looking like two different products.
    //
    // DewKnob is told whether it is bipolar; a stock slider cannot be, so the
    // range says it instead. A control that runs from below zero to above it is
    // a pan, a detune or an EQ gain, and every one of them should fill out from
    // the centre. Without this the mixer's pan knob and the instrument panel's
    // drew a half-turned arc at dead centre while the channel rack's, which is a
    // DewKnob, drew the empty ring they all should.
    const auto range = slider.getRange();
    const auto bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;

    // And the same for the function colour: a DewKnob is told its role by the
    // ParamSpec it was built from, a stock slider has to be told through the
    // colour id JUCE already has for a value track. The constructor above sets
    // that id to `accent` for every slider, so one that says nothing is
    // unchanged and one that has a role is a single setColour at its call site.
    paint::rotary (g, juce::Rectangle<int> (x, y, width, height).toFloat(), sliderPos,
                   slider.isEnabled(), bipolar, slider.findColour (juce::Slider::trackColourId));
}

void DewLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (tokens::stroke::whisper);
    const auto corner = tokens::radius::sm;

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (tokens::emphasis::pressLift);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (tokens::emphasis::controlLift);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (button.getToggleState() ? tokens::colour::accent : tokens::colour::divider);
    g.drawRoundedRectangle (bounds, corner, tokens::stroke::hairline);
}

void DewLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                                    bool isMouseDown)
{
    using namespace tokens;

    auto area = button.getLocalBounds();
    const auto active = button.getToggleState();

    // A tab reads as a surface that is either lifted (active), warmed (hover)
    // or flush (at rest) - the same three states every other row in dew uses.
    g.setColour (active        ? colour::surface
                 : isMouseDown ? colour::surfaceHover
                 : isMouseOver ? colour::surfaceRaised
                               : colour::background);
    g.fillRect (area);

    if (active)
    {
        g.setColour (colour::accent);
        g.fillRect (area.removeFromBottom (2));
    }
    else
    {
        g.setColour (colour::divider);
        g.drawVerticalLine (area.getRight() - 1, (float) area.getY() + 6.0f,
                            (float) area.getBottom() - 6.0f);
    }

    g.setColour (active        ? colour::textPrimary
                 : isMouseOver ? colour::textPrimary.withAlpha (emphasis::strong)
                               : colour::textSecondary);
    g.setFont (type::font (type::body, active));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred,
                false);
}

int DewLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int)
{
    // Room for the label plus a consistent pair of gutters, rather than V4's
    // depth-derived guess.
    const auto text = juce::GlyphArrangement::getStringWidthInt (
        tokens::type::font (tokens::type::body, true), button.getButtonText());

    return juce::jmax (72, text + tokens::space::xl * 2);
}

void DewLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g,
                                                   int width, int height)
{
    // Nothing opaque. JUCE hosts this in a component that spans the whole bar
    // and sits ABOVE every tab except the front one, so filling it - which the
    // first version of this did - hid the other three tabs completely. V4 draws
    // a shadow here; dew wants only the hairline under the strip.
    g.setColour (tokens::colour::dividerStrong);
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

// --- combo boxes -------------------------------------------------------------

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

    g.setColour (box.hasKeyboardFocus (false) ? colour::accent
                 : over                       ? colour::outline.brighter (emphasis::controlLift)
                                              : colour::outline);
    g.drawRoundedRectangle (bounds, radius::md, stroke::hairline);

    // The app's own chevron rather than JUCE's triangle.
    const auto chevron = juce::Rectangle<float> (bounds.getRight() - 26.0f,
                                                 bounds.getCentreY() - 8.0f, 16.0f, 16.0f);

    icons::draw (g, icons::chevronDown(), chevron,
                 box.isEnabled() ? colour::textSecondary : colour::textDisabled);
}

void DewLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Room on the right for the chevron, and the same inset a DewButton uses.
    label.setBounds (tokens::space::lg, 0, juce::jmax (0, box.getWidth() - tokens::space::lg - 30),
                     box.getHeight());
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
                                        const juce::String& shortcutKeyText, const juce::Drawable*,
                                        const juce::Colour*)
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

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : size::controlHeight;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (type::font (type::body), text)
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
