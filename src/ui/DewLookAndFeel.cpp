#include "DewLookAndFeel.h"

namespace dew
{

const juce::Colour Palette::background { 0xff17191d };
const juce::Colour Palette::panel      { 0xff22252b };
const juce::Colour Palette::panelDark  { 0xff121417 };
const juce::Colour Palette::line       { 0xff2c3037 };
const juce::Colour Palette::lineStrong { 0xff3d434d };
const juce::Colour Palette::text       { 0xffe6e8ec };
const juce::Colour Palette::textDim    { 0xff8b929e };
const juce::Colour Palette::accent     { 0xff4fa3ff };
const juce::Colour Palette::playhead   { 0xffffc857 };
const juce::Colour Palette::beat       { 0xff1c1f24 };

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
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto thickness = juce::jmax (2.0f, radius * 0.22f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Palette::panelDark);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                         0.0f, rotaryStartAngle, angle, true);
    g.setColour (slider.isEnabled() ? Palette::accent : Palette::lineStrong);
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.startNewSubPath (centre.x, centre.y);
    pointer.lineTo (centre.x + (radius - thickness) * std::sin (angle),
                    centre.y - (radius - thickness) * std::cos (angle));
    g.setColour (Palette::text);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, thickness * 0.4f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
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

} // namespace dew
