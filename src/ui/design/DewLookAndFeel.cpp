#include "ui/design/DewLookAndFeel.h"

#include "ui/design/Focus.h"
#include "ui/design/Icons.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Tokens.h"

#include "ui/primitives/ButtonBehaviour.h"
#include "ui/primitives/DewButtons.h"
#include "ui/primitives/DewKnob.h"
#include "ui/primitives/DewPaint.h"
#include "ui/design/Animator.h"

namespace dew
{

DewLookAndFeel::DewLookAndFeel()
{
    applyPalette();
}

void DewLookAndFeel::applyPalette()
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

namespace
{

/** The + or the - on an IncDecButtons slider, refusing the right button.

    A slider creates these for itself, so a DewSlider subclass cannot reach
    them - this is the only place they can be given the rule every other button
    in dew follows.
*/
using SliderStepButton = PopupSafeButton<juce::TextButton>;

} // namespace

juce::Button* DewLookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    // The same two glyphs LookAndFeel_V2 uses, so nothing about the control
    // moves; only what it does with a right press.
    return new SliderStepButton (isIncrement ? "+" : "-");
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

namespace
{

/** Whether this Label is the text box a Slider made for itself.

    By its PARENT rather than by a component id or a name: the Label is
    LookAndFeel_V4's own private SliderLabelComp, which cannot be constructed or
    subclassed from here, so what it is is where it lives.
*/
bool isSliderTextBox (const juce::Label& label)
{
    return dynamic_cast<const juce::Slider*> (label.getParentComponent()) != nullptr;
}

} // namespace

void DewLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! isSliderTextBox (label))
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    // The same recipe every other input takes, and the reason it is a shared
    // painter: the fill and the edge are decided in one place for the field,
    // the search field and this.
    paint::inputBox (g, label,
                     label.isEnabled() ? tokens::colour::surfaceRaised : tokens::colour::surface,
                     tokens::colour::outline);

    if (label.isBeingEdited())
        return;

    // Drawn here rather than by delegating, because the base would paint its
    // own square background and edge underneath - and the two colour ids that
    // decide those are the ones this override exists to stop using.
    const auto alpha = label.isEnabled() ? 1.0f : tokens::emphasis::dimmed;

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.setFont (getLabelFont (label));

    const auto text = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

    g.drawFittedText (label.getText(), text, label.getJustificationType(), 1,
                      label.getMinimumHorizontalScale());
}

void DewLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height)
                            .toFloat()
                            .reduced (tokens::stroke::whisper);

    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (bounds, tokens::radius::sm);
}

void DewLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                            juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto bounds = juce::Rectangle<int> (0, 0, width, height)
                            .toFloat()
                            .reduced (tokens::stroke::whisper);

    // Focused draws the thicker edge, which is what V4 does too - the shape is
    // the only thing that changes here.
    g.setColour (editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (bounds, tokens::radius::sm,
                            editor.hasKeyboardFocus (true) ? tokens::stroke::regular
                                                           : tokens::stroke::hairline);
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

    // A rule on the LEADING edge of every tab but the first, spanning the same
    // depth the tab's own background does.
    //
    // It used to be drawn on an INACTIVE tab's trailing edge, inset six pixels
    // top and bottom - so the boundary beside the selected tab had no rule at
    // all (the active tab drew none, and the tab before it is the one that
    // would have), and the rules that did appear were a short stroke floating
    // beside a block of colour running the bar's full height. A boundary
    // belongs to the PAIR it separates, not to the state of one of them.
    //
    // Before the underline below, so the accent keeps the last rows for itself
    // rather than being notched by a rule crossing it.
    if (button.getIndex() > 0)
    {
        g.setColour (colour::divider);
        g.drawVerticalLine (area.getX(), (float) area.getY(), (float) area.getBottom());
    }

    if (active)
    {
        g.setColour (colour::accent);
        g.fillRect (area.removeFromBottom (juce::roundToInt (stroke::bold)));
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

} // namespace dew
