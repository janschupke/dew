#include "DewControls.h"

namespace dew
{

using namespace tokens;

namespace
{

juce::Colour fillFor (bool enabled, bool highlighted, bool down, juce::Colour base)
{
    if (! enabled)  return base.withMultipliedSaturation (0.3f).withMultipliedBrightness (0.7f);
    if (down)       return base.brighter (0.22f);
    if (highlighted) return base.brighter (0.10f);
    return base;
}

} // namespace

// --- DewButton ---------------------------------------------------------------

DewButton::DewButton (const juce::String& text, Role r)
    : juce::Button (text), role (r)
{
    setButtonText (text);
}

void DewButton::setRole (Role r)
{
    role = r;
    repaint();
}

void DewButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    juce::Colour background, text, border;

    switch (role)
    {
        case Role::primary:
            background = on ? colour::accent : colour::accentMuted;
            text = colour::textOnAccent;
            border = colour::accent;
            break;

        case Role::danger:
            background = on ? colour::danger : colour::surfaceRaised;
            text = on ? colour::textOnAccent : colour::danger;
            border = colour::danger.withAlpha (0.6f);
            break;

        case Role::ghost:
            background = highlighted || down ? colour::surfaceRaised : juce::Colours::transparentBlack;
            text = colour::textSecondary;
            border = juce::Colours::transparentBlack;
            break;

        case Role::normal:
        default:
            background = on ? colour::accent : colour::surfaceRaised;
            text = on ? colour::textOnAccent : colour::textPrimary;
            border = on ? colour::accent : colour::outline;
            break;
    }

    g.setColour (fillFor (isEnabled(), highlighted, down, background));
    g.fillRoundedRectangle (bounds, radius::sm);

    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);
    }

    g.setColour (isEnabled() ? text : colour::textDisabled);
    g.setFont (type::font (type::body));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

// --- DewIconButton -----------------------------------------------------------

DewIconButton::DewIconButton (juce::Path i, const juce::String& tooltipText)
    : juce::Button (tooltipText), icon (std::move (i))
{
    setTooltip (tooltipText);
}

void DewIconButton::setIcon (juce::Path i)
{
    icon = std::move (i);
    repaint();
}

void DewIconButton::setOnColour (juce::Colour c)
{
    onColour = c;
    repaint();
}

void DewIconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    const auto background = on ? onColour
                               : (highlighted || down ? colour::surfaceHover : colour::surfaceRaised);

    g.setColour (fillFor (isEnabled(), highlighted, down, background));
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (on ? onColour : colour::outline);
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    const auto tint = ! isEnabled() ? colour::textDisabled
                                    : (on ? colour::textOnAccent : colour::textPrimary);

    icons::draw (g, icon, bounds.reduced (bounds.getWidth() * 0.28f), tint);
}

// --- DewLetterToggle ---------------------------------------------------------

DewLetterToggle::DewLetterToggle (const juce::String& l, juce::Colour c,
                                  const juce::String& tooltipText)
    : juce::Button (tooltipText), letter (l), onColour (c)
{
    setClickingTogglesState (true);
    setTooltip (tooltipText);
}

void DewLetterToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    g.setColour (fillFor (isEnabled(), highlighted, down,
                          on ? onColour : colour::surfaceRaised));
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (on ? onColour : colour::outline);
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    g.setColour (on ? colour::textOnAccent : colour::textSecondary);
    g.setFont (type::font (type::small, true));
    g.drawText (letter, getLocalBounds(), juce::Justification::centred, false);
}

// --- DewKnob -----------------------------------------------------------------

namespace
{

/** Suppresses the host LookAndFeel's rotary so DewKnob can draw its own. The
    slider stays for its behaviour - drag, wheel, keyboard, double-click reset -
    but contributes no pixels.
*/
class InvisibleRotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider&) override {}
};

InvisibleRotaryLookAndFeel& invisibleRotary()
{
    static InvisibleRotaryLookAndFeel instance;
    return instance;
}

} // namespace

DewKnob::DewKnob (const juce::String& c, double minimum, double maximum, double interval)
    : caption (c)
{
    slider.setRange (minimum, maximum, interval);
    slider.setLookAndFeel (&invisibleRotary());
    slider.onValueChange = [this] { if (onValueChange != nullptr) onValueChange(); repaint(); };
    slider.onDragStart = [this] { if (onEditStart != nullptr) onEditStart(); };
    addAndMakeVisible (slider);
}

void DewKnob::setValue (double v, juce::NotificationType notification)
{
    slider.setValue (v, notification);
    repaint();
}

void DewKnob::setNumDecimalPlaces (int places)
{
    decimalPlaces = juce::jmax (0, places);
    repaint();
}

void DewKnob::setBipolar (bool shouldBeBipolar)
{
    bipolar = shouldBeBipolar;
    repaint();
}

void DewKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (13);        // caption
    area.removeFromBottom (14);     // value
    slider.setBounds (area);
}

void DewKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (caption, area.removeFromTop (13), juce::Justification::centred, false);

    auto valueArea = area.removeFromBottom (14);

    const auto range = slider.getRange();
    const auto span = range.getLength();
    const auto proportion = span > 0.0 ? (float) ((slider.getValue() - range.getStart()) / span)
                                       : 0.0f;

    paint::rotary (g, area.toFloat(), proportion, isEnabled(), bipolar);

    g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText (juce::String (slider.getValue(), decimalPlaces), valueArea,
                juce::Justification::centred, false);
}

// --- DewPanel ----------------------------------------------------------------

DewPanel::DewPanel (juce::String t)
    : title (std::move (t))
{
}

void DewPanel::setTitle (juce::String t)
{
    title = std::move (t);
    repaint();
}

juce::Rectangle<int> DewPanel::getContentBounds() const
{
    auto area = getLocalBounds().reduced (space::lg);

    if (title.isNotEmpty())
        area.removeFromTop (24);

    return area;
}

void DewPanel::paint (juce::Graphics& g)
{
    g.setColour (colour::surface);
    g.fillAll();

    if (title.isNotEmpty())
    {
        auto heading = getLocalBounds().reduced (space::lg).removeFromTop (24);

        g.setColour (colour::textPrimary);
        g.setFont (type::font (type::title, true));
        g.drawText (title, heading, juce::Justification::centredLeft, false);

        g.setColour (colour::divider);
        g.drawHorizontalLine (heading.getBottom(), (float) space::lg,
                              (float) (getWidth() - space::lg));
    }
}

// --- shared painting ---------------------------------------------------------

void forwardChildMouseEventsTo (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        child->addMouseListener (&parent, /* wantsEventsForAllNestedChildComponents */ true);
}

namespace paint
{

void rotary (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion,
             bool enabled, bool bipolar)
{
    constexpr auto startAngle = juce::MathConstants<float>::pi * 1.2f;
    constexpr auto endAngle   = juce::MathConstants<float>::pi * 2.8f;

    const auto square = bounds.withSizeKeepingCentre (juce::jmin (bounds.getWidth(),
                                                                  bounds.getHeight()),
                                                      juce::jmin (bounds.getWidth(),
                                                                  bounds.getHeight()))
                            .reduced (2.0f);

    const auto radius = square.getWidth() * 0.5f;
    const auto centre = square.getCentre();
    const auto thickness = juce::jmax (2.5f, radius * 0.26f);
    const auto arcRadius = radius - thickness * 0.5f;
    const auto angle = startAngle + juce::jlimit (0.0f, 1.0f, proportion) * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    // The track has to read as a control even when the value arc is empty. It
    // used to be `well`, which on a `surface` panel is nearly invisible, so a
    // knob at the bottom of its range looked like a stray tick mark rather than
    // a knob - which is exactly how the ADSR knobs at their minimums looked.
    g.setColour (colour::outline.withAlpha (0.6f));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // A bipolar control fills out from the centre, so "no pan" reads as no fill
    // rather than as half a ring.
    const auto fillFrom = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (! juce::approximatelyEqual (angle, fillFrom))
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (fillFrom, angle), juce::jmax (fillFrom, angle), true);
        g.setColour (enabled ? colour::accent : colour::dividerStrong);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Path pointer;
    pointer.startNewSubPath (centre.x + (radius - thickness * 1.9f) * std::sin (angle),
                             centre.y - (radius - thickness * 1.9f) * std::cos (angle));
    pointer.lineTo (centre.x + (radius - thickness * 0.2f) * std::sin (angle),
                    centre.y - (radius - thickness * 0.2f) * std::cos (angle));

    g.setColour (enabled ? colour::textPrimary : colour::textDisabled);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, thickness * 0.42f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void surface (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (bounds);
}

void wellBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (colour::well);
    g.fillRect (bounds);
}

void inertArea (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Darker than the well, with a faint diagonal hatch. The point is that a
    // large empty region should read as "there is nothing here", not as a
    // control that has stopped responding - which is exactly how the old
    // channel rack's 88% of empty panel read.
    if (bounds.isEmpty())
        return;

    g.setColour (colour::wellDeep);
    g.fillRect (bounds);

    g.setColour (colour::divider.withAlpha (0.25f));

    const auto spacing = 12.0f;
    const auto span = (float) (bounds.getWidth() + bounds.getHeight());

    for (float offset = 0.0f; offset < span; offset += spacing)
    {
        const auto x = (float) bounds.getX() + offset;
        g.drawLine (x, (float) bounds.getY(),
                    x - (float) bounds.getHeight(), (float) bounds.getBottom(), 0.5f);
    }
}

void caption (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
              juce::Justification justification)
{
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (text, bounds, justification, false);
}

} // namespace paint

} // namespace dew
