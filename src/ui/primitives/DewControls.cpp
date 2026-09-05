#include "ui/primitives/DewControls.h"

#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Keys.h"

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
                        : button.isDown()    ? emphasis::pressLift
                        : button.isOver()    ? emphasis::controlLift
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
    : PopupSafeButton<juce::Button> (text)
    , role (r)
{
    setButtonText (text);
    setMouseCursor (cursor::clickable);
}

void DewButton::setTextJustification (juce::Justification j)
{
    justification = j;
    repaint();
}

void DewButton::setGlyph (juce::Path p)
{
    glyph = std::move (p);
    repaint();
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
            background = highlighted || down ? colour::surfaceRaised
                                             : juce::Colours::transparentBlack;
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

    auto label = justification == juce::Justification::centred
                     ? getLocalBounds()
                     : getLocalBounds().reduced (space::md, 0);

    if (! glyph.isEmpty())
    {
        // The glyph and the word are placed as one group so a centred button
        // stays centred. Measured rather than guessed: a fixed inset would put
        // the pair off centre by however wide the word happened to be.
        const auto column = size::glyphColumn + space::xs;
        const auto word = juce::GlyphArrangement::getStringWidthInt (type::font (type::body),
                                                                     getButtonText());
        auto group = label.withSizeKeepingCentre (juce::jmin (label.getWidth(), column + word),
                                                  label.getHeight());

        if (justification != juce::Justification::centred)
            group.setX (label.getX());

        icons::draw (g, glyph,
                     group.removeFromLeft (size::glyphColumn)
                         .toFloat()
                         .withSizeKeepingCentre ((float) size::glyphMark, (float) size::glyphMark),
                     isEnabled() ? text : colour::textDisabled);

        group.removeFromLeft (space::xs);
        label = group;
    }

    g.drawText (getButtonText(), label, justification, false);

    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}

// --- DewIconButton -----------------------------------------------------------

DewIconButton::DewIconButton (juce::Path i, const juce::String& tooltipText, Role r)
    : PopupSafeButton<juce::Button> (tooltipText)
    , icon (std::move (i))
    , role (r)
{
    setTooltip (tooltipText);
    setMouseCursor (cursor::clickable);
}

void DewIconButton::setTooltip (const juce::String& text)
{
    juce::SettableTooltipClient::setTooltip (text);
    setTitle (text);
}

void DewIconButton::setRole (Role r)
{
    role = r;
    repaint();
}

void DewIconButton::setIcon (juce::Path i)
{
    icon = std::move (i);
    repaint();
}

juce::Colour DewIconButton::restingTint() const
{
    switch (role)
    {
        case Role::go: return colour::success;
        case Role::record: return colour::recording;
        case Role::danger: return colour::danger;
        case Role::neutral: break;
    }

    return colour::textPrimary;
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
    const auto drain = [this] (juce::Colour c) { return isEnabled() ? c : emphasis::disabled (c); };

    g.setColour (drain (lift.apply (off, onColour)));
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (drain (lift.cross (colour::outline, onColour)));
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    // The role colours the glyph at rest only. A toggled button crosses to
    // textOnAccent because the fill has crossed to onColour underneath it, and
    // a red glyph on a filled button would be unreadable rather than emphatic.
    const auto tint = ! isEnabled() ? colour::textDisabled
                                    : lift.cross (restingTint(), colour::textOnAccent);

    icons::draw (g, icon, bounds.reduced (bounds.getWidth() * 0.28f), tint);

    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}

// --- DewLetterToggle ---------------------------------------------------------

DewLetterToggle::DewLetterToggle (const juce::String& l, juce::Colour c,
                                  const juce::String& tooltipText)
    : PopupSafeButton<juce::Button> (tooltipText)
    , letter (l)
    , onColour (c)
{
    setClickingTogglesState (true);
    setTooltip (tooltipText);
    setMouseCursor (cursor::clickable);
}

void DewDropdown::setTooltip (const juce::String& text)
{
    // juce::ComboBox's own override, NOT SettableTooltipClient's: a ComboBox
    // keeps its tooltip on the juce::Label inside it and reads getTooltip back
    // from there, so setting the base member alone stores a string nothing ever
    // returns. The hover-help gate caught exactly that.
    juce::ComboBox::setTooltip (text);
    setTitle (text);
}

void DewLetterToggle::setTooltip (const juce::String& text)
{
    juce::SettableTooltipClient::setTooltip (text);
    setTitle (text);
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

    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}

DewCheckbox::DewCheckbox (const juce::String& text)
    : PopupSafeButton<juce::ToggleButton> (text)
{
    setMouseCursor (cursor::clickable);
}

DewDropdown::DewDropdown (const juce::String& name)
    : juce::ComboBox (name)
{
    setMouseCursor (cursor::clickable);
}

// --- DewSlider ---------------------------------------------------------------

bool DewSlider::keyPressed (const juce::KeyPress& key)
{
    const auto command = keys::valueKeys::commandFor (key);
    const auto direction = keys::valueKeys::directionOf (command);

    if (direction == 0)
        return false;

    const auto interval = getInterval();
    const auto fraction = keys::valueKeys::fractionFor (command, key.getModifiers(), interval);

    // proportionOfLengthToValue and its inverse ARE the skewed range's own
    // conversions, so a step is one per cent of the travel rather than of the
    // span: on a cutoff that is one per cent in pitch, not a flat 180Hz that is
    // inaudible at the top and a leap at the bottom.
    //
    // Read off the SLIDER rather than off the ParamSpec on purpose. DewKnob
    // fits a power skew whose geometric mean lands at 0.5; ParamSpec::
    // fromNormalised is a true exponential. They agree at nought, a half and
    // one and differ in between, so taking the range makes the keyboard agree
    // with the DRAG on the same control - which is the pair a hand notices.
    const auto moved = keys::valueKeys::steppedValue (
        getValue(), fraction, direction, interval,
        [this] (double v) { return valueToProportionOfLength (v); },
        [this] (double p) { return proportionOfLengthToValue (p); });

    // setValue clamps and snaps through constrainedValue, so a step that
    // overshoots an end lands on it.
    setValue (moved, juce::sendNotificationSync);

    return true;
}

void DewKnob::mouseDown (const juce::MouseEvent& event)
{
    // A knob needs no latch of its own: this runs as a LISTENER on the slider
    // inside it, and the slider is a DewSlider, which refuses the right button
    // for the whole press on its own account. Opening the menu is all that is
    // left to do here.
    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    slider.setMouseDragSensitivity (gesture::dragPixelsFor (event.mods));
}

} // namespace dew
