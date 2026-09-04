#include "ui/primitives/DewControls.h"

#include "ui/design/Cursors.h"
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
    : juce::Button (text)
    , role (r)
{
    setButtonText (text);
    setMouseCursor (cursor::clickable);
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
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);

    paint::focusRing (g, *this, hasKeyboardFocus (true));
}

// --- DewIconButton -----------------------------------------------------------

DewIconButton::DewIconButton (juce::Path i, const juce::String& tooltipText, Role r)
    : juce::Button (tooltipText)
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

    paint::focusRing (g, *this, hasKeyboardFocus (true));
}

// --- DewLetterToggle ---------------------------------------------------------

DewLetterToggle::DewLetterToggle (const juce::String& l, juce::Colour c,
                                  const juce::String& tooltipText)
    : juce::Button (tooltipText)
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

    paint::focusRing (g, *this, hasKeyboardFocus (true));
}

/** One rule for every control: a right-click opens the menu if there is one,
    and is swallowed either way.

    Above each class's own handling rather than inside it, so a right-click never
    arms a drag or a toggle that then never completes.

    Swallowing it even with NO menu is the point, and is what this used to get
    wrong. juce::Button completes a click for whichever mouse button pressed it,
    so falling through on a null hook meant every button in dew without a
    context menu - play, stop, record, delete pattern, every tool and every zoom
    - fired on a right-click. A person aiming at a menu that is not there asked
    for nothing, not for the button.
*/
static bool consumePopupPress (const juce::MouseEvent& event, const std::function<void()>& hook)
{
    if (! event.mods.isPopupMenu())
        return false;

    if (hook != nullptr)
        hook();

    return true;
}

void DewButton::mouseDown (const juce::MouseEvent& event)
{
    if (consumePopupPress (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

void DewIconButton::mouseDown (const juce::MouseEvent& event)
{
    if (consumePopupPress (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

void DewLetterToggle::mouseDown (const juce::MouseEvent& event)
{
    if (consumePopupPress (event, onContextMenu))
        return;

    juce::Button::mouseDown (event);
}

DewCheckbox::DewCheckbox (const juce::String& text)
    : juce::ToggleButton (text)
{
    setMouseCursor (cursor::clickable);
}

DewDropdown::DewDropdown (const juce::String& name)
    : juce::ComboBox (name)
{
    setMouseCursor (cursor::clickable);
}

void DewCheckbox::mouseDown (const juce::MouseEvent& event)
{
    // No hook: a checkbox names no parameter, so there is nothing to offer. The
    // press is still consumed, because the alternative is toggling it.
    if (event.mods.isPopupMenu())
        return;

    juce::ToggleButton::mouseDown (event);
}

void DewKnob::mouseDown (const juce::MouseEvent& event)
{
    if (consumePopupPress (event, onContextMenu))
        return;

    const auto pixels = gesture::isFine (event.mods)
                            ? (int) ((double) gesture::dragPixelsForFullRange
                                     / gesture::fineMultiplier)
                            : gesture::dragPixelsForFullRange;

    slider.setMouseDragSensitivity (pixels);
}

} // namespace dew
