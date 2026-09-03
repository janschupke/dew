#include "ui/primitives/DewControls.h"

#include "ui/design/Gestures.h"

#include <cmath>

namespace dew
{

using namespace tokens;

namespace
{

} // namespace

// --- ButtonLift --------------------------------------------------------------

void ButtonLift::update()
{
    toggled.animateTo (button.getToggleState() ? 1.0f : 0.0f, motion::quickMs);

    const auto target = ! button.isEnabled() ? 0.0f
                      : button.isDown()      ? emphasis::pressLift
                      : button.isOver()      ? emphasis::controlLift
                                             : 0.0f;

    // A press is quicker to arrive than to leave, which is what makes a button
    // feel like it answers rather than like it catches up: accelerate INTO the
    // press, decelerate back out of it.
    motion.animateTo (target, motion::quickMs,
                      target > motion.get() ? Ease::accelerate : Ease::standard);
}

juce::Colour ButtonLift::apply (juce::Colour base) const
{
    if (! button.isEnabled())
        return emphasis::disabled (base);

    return base.brighter (motion.get());
}

juce::Colour ButtonLift::apply (juce::Colour off, juce::Colour on) const
{
    return apply (cross (off, on));
}

juce::Colour ButtonLift::cross (juce::Colour off, juce::Colour on) const
{
    return off.interpolatedWith (on, juce::jlimit (0.0f, 1.0f, toggled.get()));
}

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
    const auto bounds = paint::bodyRect (*this);
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
            border = colour::danger.withAlpha (emphasis::dimmed);
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

    g.setColour (lift.apply (background));
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
    const auto bounds = paint::bodyRect (*this);

    const auto off = highlighted || down ? colour::surfaceHover : colour::surfaceRaised;

    // Disabled drains the WHOLE button, not only its glyph.
    //
    // It used to dim the icon and leave the fill and the outline at full
    // strength, which reads as an ordinary button whose icon happens to be
    // faint - so a disabled one looked usable, and every small-glyph button
    // that was perfectly usable looked disabled. emphasis::disabled is the
    // system's word for "cannot be used", and it is deliberately weaker than
    // silenced: a disabled button must still read as a button.
    const auto drain = [this] (juce::Colour c)
    {
        return isEnabled() ? c : emphasis::disabled (c);
    };

    g.setColour (drain (lift.apply (off, onColour)));
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (drain (lift.cross (colour::outline, onColour)));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    const auto tint = ! isEnabled() ? colour::textDisabled
                                    : lift.cross (colour::textPrimary, colour::textOnAccent);

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

void DewLetterToggle::paintButton (juce::Graphics& g, bool, bool)
{
    const auto bounds = paint::bodyRect (*this);

    g.setColour (lift.apply (colour::surfaceRaised, onColour));
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (lift.cross (colour::outline, onColour));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    g.setColour (lift.cross (colour::textSecondary, colour::textOnAccent));
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

    // Never called anywhere before this, so every knob in dew sat on JUCE's
    // default of 250 - which is not the same as having chosen 250.
    slider.setMouseDragSensitivity (gesture::dragPixelsForFullRange);
    slider.onValueChange = [this]
    {
        updateNeedle();

        if (onValueChange != nullptr)
            onValueChange();

        repaint();
    };

    slider.onDragStart = [this] { dragging = true; if (onEditStart != nullptr) onEditStart(); };
    slider.onDragEnd = [this] { dragging = false; if (onEditEnd != nullptr) onEditEnd(); };

    // The slider is what the pointer is actually over, so the knob listens to
    // it rather than to itself.
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
}


/** One rule for all four controls: a right-click that has somewhere to go opens
    the menu and consumes the press.

    Above each class's own handling rather than inside it, so a right-click never
    arms a drag or a toggle that then never completes.
*/
static bool openContextMenu (const juce::MouseEvent& event, const std::function<void()>& hook)
{
    if (! event.mods.isPopupMenu() || hook == nullptr)
        return false;

    hook();
    return true;
}

void DewButton::mouseDown (const juce::MouseEvent& event)
{
    if (openContextMenu (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

void DewIconButton::mouseDown (const juce::MouseEvent& event)
{
    if (openContextMenu (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

void DewLetterToggle::mouseDown (const juce::MouseEvent& event)
{
    if (openContextMenu (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

void DewKnob::mouseDown (const juce::MouseEvent& event)
{
    if (openContextMenu (event, onContextMenu))
        return;

    const auto pixels = gesture::isFine (event.mods)
                            ? (int) ((double) gesture::dragPixelsForFullRange / gesture::fineMultiplier)
                            : gesture::dragPixelsForFullRange;

    slider.setMouseDragSensitivity (pixels);
}

DewKnob::DewKnob (const ParamSpec& spec)
    : DewKnob (spec.caption, spec.minimum, spec.maximum, spec.interval)
{
    // The catalog's displayName, which has said "the automation picker,
    // tooltips" in its own comment since it was written and reached only the
    // first of those. A caption says what a knob is CALLED; the tooltip is what
    // the status bar shows the moment the pointer arrives, and a compact knob
    // has no caption at all - its own header says so and it had nothing to say
    // it with.
    setTooltip (spec.displayName);

    setNumDecimalPlaces (spec.decimals);
    setBipolar (spec.bipolar);

    // A logarithmic parameter gets a NormalisableRange, not a plain one: half a
    // millisecond to ten seconds is four and a half decades, and linearly every
    // usable value lives in the first one per cent of the travel.
    if (spec.curve == ParamCurve::logarithmic && spec.minimum > 0.0)
    {
        const auto skew = std::log (0.5) / std::log ((std::sqrt (spec.minimum * spec.maximum)
                                                      - spec.minimum)
                                                     / (spec.maximum - spec.minimum));

        slider.setNormalisableRange ({ spec.minimum, spec.maximum, spec.interval, skew });
    }
}

void DewKnob::setValue (double v, juce::NotificationType notification)
{
    slider.setValue (v, notification);

    // Not only from onValueChange. The notification type says whether
    // LISTENERS hear about the change; the knob's own needle is not a listener,
    // and dontSendNotification - which every refresh() in dew uses - would
    // otherwise leave it pointing at the last value it was told about.
    updateNeedle();
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

void DewKnob::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    resized();
    repaint();
}

void DewKnob::setTooltip (const juce::String& text)
{
    juce::SettableTooltipClient::setTooltip (text);
    slider.setTooltip (text);
}

void DewKnob::resized()
{
    auto area = getLocalBounds();

    if (! compact)
    {
        area.removeFromTop (size::knobCaption);   // caption
        area.removeFromBottom (size::knobValue);  // value
    }

    slider.setBounds (area);
}

float DewKnob::proportionOfValue() const
{
    const auto range = slider.getRange();
    const auto span = range.getLength();

    return span > 0.0 ? (float) ((slider.getValue() - range.getStart()) / span) : 0.0f;
}

void DewKnob::updateNeedle()
{
    const auto proportion = proportionOfValue();

    if (dragging || ! needleSeeded)
    {
        needle.snapTo (proportion);
        needleSeeded = true;
        return;
    }

    needle.animateTo (proportion, motion::valueMs, Ease::decelerate);
}

void DewKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    // Seeded here as well as on the first change, because a knob can be laid
    // out and painted before anything ever writes to it - and the seed has to
    // be the value it HAS, not zero.
    if (! needleSeeded)
    {
        needle.snapTo (proportionOfValue());
        needleSeeded = true;
    }

    const auto proportion = needle.get();

    // A compact knob has room for the rotary and nothing else. Its caption
    // lives in the tooltip and its meaning in the fill: unipolar volume fills
    // from the left, bipolar pan from the centre.
    if (compact)
    {
        paint::rotary (g, area.toFloat(), proportion, isEnabled(), bipolar);
        return;
    }

    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (caption, area.removeFromTop (size::knobCaption), juce::Justification::centred, false);

    auto valueArea = area.removeFromBottom (size::knobValue);

    paint::rotary (g, area.toFloat(), proportion, isEnabled(), bipolar);

    g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
    g.setFont (type::font (type::caption));
    g.drawText (juce::String (slider.getValue(), decimalPlaces), valueArea,
                juce::Justification::centred, false);
}


// --- shared painting ---------------------------------------------------------

void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (type::font (type::caption));
    label.setColour (juce::Label::textColourId, colour::textSecondary);
    label.setJustificationType (juce::Justification::centred);

    // A caption labels the control beside it; the pointer belongs to that
    // control, and a Label that ate the press would take its hover help with it.
    label.setInterceptsMouseClicks (false, false);
}

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
    g.setColour (colour::outline.withAlpha (emphasis::dimmed));
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
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (stroke::regular, thickness * 0.42f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void surface (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour c)
{
    g.setColour (c);
    g.fillRect (bounds);
}

void container (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;

    const auto body = bounds.toFloat().reduced (stroke::whisper);

    g.setColour (colour::surface);
    g.fillRoundedRectangle (body, radius::md);

    g.setColour (colour::outline);
    g.drawRoundedRectangle (body, radius::md, stroke::hairline);
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

    g.setColour (colour::divider.withAlpha (emphasis::hatch));

    const auto spacing = 12.0f;
    const auto span = (float) (bounds.getWidth() + bounds.getHeight());

    for (float offset = 0.0f; offset < span; offset += spacing)
    {
        const auto x = (float) bounds.getX() + offset;
        g.drawLine (x, (float) bounds.getY(),
                    x - (float) bounds.getHeight(), (float) bounds.getBottom(), 0.5f);
    }
}

void beyondEnd (juce::Graphics& g, juce::Rectangle<int> bounds, float edgeX)
{
    if (bounds.isEmpty())
        return;

    // A scrim rather than a fill: the grid underneath stays visible, so the
    // view reads as continuing rather than as ending in a void, while still
    // being obviously not part of the pattern.
    g.setColour (colour::wellDeep.withAlpha (emphasis::dimmed));
    g.fillRect (bounds);

    // The edge itself carries the meaning, so it is drawn at full strength.
    g.setColour (colour::dividerStrong);
    g.fillRect (juce::Rectangle<float> (edgeX - 1.0f, (float) bounds.getY(),
                                        2.0f, (float) bounds.getHeight()));
}

void caption (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
              juce::Justification justification)
{
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::caption));
    g.drawText (text, bounds, justification, false);
}

void sectionHeading (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                     juce::Justification justification)
{
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::small, true));
    g.drawText (text, bounds, justification, false);
}

void emptyState (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                 juce::Justification justification)
{
    // textSecondary, not textDisabled. An empty state is the one thing on an
    // empty panel, so it is the opposite of de-emphasised - it is the only
    // instruction the user has.
    g.setColour (colour::textSecondary);
    g.setFont (type::font (type::body));
    g.drawText (text, bounds, justification, false);
}

juce::Rectangle<float> bodyRect (const juce::Component& c)
{
    return c.getLocalBounds().toFloat().reduced (stroke::whisper);
}

void waveform (juce::Graphics& g, juce::Range<float> y, juce::Range<float> span,
               juce::Range<float> painted, const WaveformPeaks& peaks,
               const std::function<juce::Colour (float x)>& colourAt)
{
    if (peaks.isEmpty() || span.getLength() <= 0.0f)
        return;

    const auto centre = y.getStart() + y.getLength() * 0.5f;
    const auto halfHeight = juce::jmax (1.0f, y.getLength() * 0.5f - (float) size::waveformInset);

    for (int x = (int) painted.getStart(); x < (int) painted.getEnd(); ++x)
    {
        const auto from = ((float) x - span.getStart()) / span.getLength();
        const auto to = ((float) (x + 1) - span.getStart()) / span.getLength();

        const auto bin = peaks.range (from, to);
        const auto top = centre - bin.maximum * halfHeight;
        const auto bottom = centre - bin.minimum * halfHeight;

        g.setColour (colourAt ((float) x));
        g.fillRect ((float) x, juce::jmin (top, bottom), 1.0f,
                    juce::jmax (1.0f, std::abs (bottom - top)));
    }
}

} // namespace paint

} // namespace dew
