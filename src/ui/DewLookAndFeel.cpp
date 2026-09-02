#include "DewLookAndFeel.h"

#include "design/Tokens.h"

#include "primitives/DewControls.h"

namespace dew
{

DewLookAndFeel::DewLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::DocumentWindow::textColourId, Palette::text);

    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::TextEditor::backgroundColourId, Palette::panelDark);
    setColour (juce::TextEditor::textColourId, Palette::text);
    setColour (juce::TextEditor::outlineColourId, Palette::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);

    setColour (juce::TextButton::buttonColourId, Palette::panel);
    setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::TextButton::textColourOnId, Palette::background);

    setColour (juce::ComboBox::backgroundColourId, Palette::panel);
    setColour (juce::ComboBox::textColourId, Palette::text);
    setColour (juce::ComboBox::outlineColourId, Palette::line);
    setColour (juce::ComboBox::arrowColourId, Palette::textDim);

    setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::background);

    setColour (juce::Slider::thumbColourId, Palette::accent);
    setColour (juce::Slider::trackColourId, Palette::accent);
    setColour (juce::Slider::backgroundColourId, Palette::panelDark);
    setColour (juce::Slider::textBoxTextColourId, Palette::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelDark);
    setColour (juce::Slider::textBoxOutlineColourId, Palette::line);

    setColour (juce::TabbedComponent::backgroundColourId, Palette::background);
    setColour (juce::TabbedComponent::outlineColourId, Palette::line);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, Palette::line);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, Palette::accent);
    setColour (juce::TabbedButtonBar::tabTextColourId, Palette::textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, Palette::text);

    setColour (juce::ScrollBar::thumbColourId, Palette::lineStrong);
    setColour (juce::ToggleButton::textColourId, Palette::text);
    setColour (juce::ToggleButton::tickColourId, Palette::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Palette::lineStrong);

    setColour (juce::AlertWindow::backgroundColourId, Palette::panel);
    setColour (juce::AlertWindow::textColourId, Palette::text);
    setColour (juce::AlertWindow::outlineColourId, Palette::line);
}

juce::Font DewLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

void DewLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float, juce::Slider& slider)
{
    // Delegates to the same routine DewKnob uses, so a stock juce::Slider and a
    // dew primitive cannot end up looking like two different products.
    paint::rotary (g, juce::Rectangle<int> (x, y, width, height).toFloat(),
                   sliderPos, slider.isEnabled(), false);
}

void DewLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 4.0f;

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (button.getToggleState() ? Palette::accent : Palette::line);
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void DewLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                    bool isMouseOver, bool isMouseDown)
{
    using namespace tokens;

    auto area = button.getLocalBounds();
    const auto active = button.getToggleState();

    // A tab reads as a surface that is either lifted (active), warmed (hover)
    // or flush (at rest) - the same three states every other row in dew uses.
    g.setColour (active ? colour::surface
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

    g.setColour (active ? colour::textPrimary
                        : isMouseOver ? colour::textPrimary.withAlpha (0.85f)
                                      : colour::textSecondary);
    g.setFont (type::font (type::body, active));
    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred, false);
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

} // namespace dew
