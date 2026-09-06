#include "ui/primitives/DewNumberField.h"

#include "ui/design/Gestures.h"
#include "ui/design/Cursors.h"
#include "ui/design/Keys.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

namespace
{
/** Pixels of vertical travel to cross the whole range. Chosen so a full sweep
    is a comfortable forearm movement rather than a mouse-lift.
*/

/** Whether this range can be travelled by ratio at all. A range that touches or
    crosses zero has no ratio to move by, which is why every branch below tests
    all three conditions rather than the flag alone. */
bool travelsByRatio (bool logarithmic, double minimum, double maximum) noexcept
{
    return logarithmic && minimum > 0.0 && maximum > minimum;
}

/** The field's own 0..1, and its inverse.

    The drag and the wheel state the same curve in their own terms - the drag as
    a ratio applied to the value it started from, the wheel as a ratio per notch
    - because each is a DELTA and this pair is a POSITION. They agree; the
    keyboard is simply the one gesture that has an absolute position to work
    from, so it is the one that can use these.

    Deliberately the same functions as ParamSpec::toNormalised and
    fromNormalised. A field is handed min, max, interval and a bool rather than
    the spec, so it restates the curve, and the two must not drift.
*/
double normalisedOf (double v, bool logarithmic, double minimum, double maximum) noexcept
{
    if (travelsByRatio (logarithmic, minimum, maximum))
        return std::log (v / minimum) / std::log (maximum / minimum);

    return maximum > minimum ? (v - minimum) / (maximum - minimum) : 0.0;
}

double valueOf (double p, bool logarithmic, double minimum, double maximum) noexcept
{
    if (travelsByRatio (logarithmic, minimum, maximum))
        return minimum * std::exp (p * std::log (maximum / minimum));

    return minimum + p * (maximum - minimum);
}
} // namespace

DewNumberField::DewNumberField()
{
    setMouseCursor (cursor::value);
    setWantsKeyboardFocus (true);
}

DewNumberField::~DewNumberField() = default;

void DewNumberField::setRange (double newMinimum, double newMaximum, double newInterval)
{
    minimum = newMinimum;
    maximum = juce::jmax (newMinimum, newMaximum);
    interval = newInterval > 0.0 ? newInterval : 0.0;
    setValue (value, juce::dontSendNotification);
}

void DewNumberField::setLogarithmic (bool shouldBeLogarithmic)
{
    logarithmic = shouldBeLogarithmic;
}

void DewNumberField::setValue (double newValue, juce::NotificationType notification)
{
    auto clamped = juce::jlimit (minimum, maximum, newValue);

    if (interval > 0.0)
        clamped = minimum + interval * std::round ((clamped - minimum) / interval);

    clamped = juce::jlimit (minimum, maximum, clamped);

    if (juce::exactlyEqual (clamped, value))
        return;

    value = clamped;
    repaint();

    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange();
}

void DewNumberField::setSuffix (juce::String newSuffix)
{
    suffix = std::move (newSuffix);
    repaint();
}

void DewNumberField::setNumDecimalPlaces (int places)
{
    decimalPlaces = juce::jmax (0, places);
    repaint();
}

void DewNumberField::setTooltip (const juce::String& text)
{
    juce::SettableTooltipClient::setTooltip (text);
    setTitle (text);
}

void DewNumberField::setFunctionColour (juce::Colour c)
{
    functionColour = c;
    repaint();
}

void DewNumberField::setCaption (juce::String newCaption)
{
    caption = std::move (newCaption);
    repaint();
}

juce::String DewNumberField::displayText() const
{
    return juce::String (value, decimalPlaces) + suffix;
}

void DewNumberField::commit (double newValue)
{
    setValue (newValue, juce::sendNotification);
}

void DewNumberField::mouseDown (const juce::MouseEvent& event)
{
    // It already declined a right-click; now it has somewhere to send one.
    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu();

        return;
    }

    dragging = true;
    valueAtDragStart = value;

    if (onEditStart != nullptr)
        onEditStart();
}

void DewNumberField::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    // Up is more, which is what every DAW does and the opposite of screen
    // coordinates, hence the negation.
    const auto travel = -(double) event.getDistanceFromDragStartY();
    const auto scale = gesture::isFine (event.mods) ? gesture::fineMultiplier : 1.0;
    const auto fraction = travel / gesture::dragPixelsForFullRange * scale;

    if (logarithmic && minimum > 0.0 && maximum > minimum)
    {
        // A constant ratio per pixel, so the low end of a frequency range is
        // reachable instead of being compressed into the first few pixels.
        const auto decades = std::log (maximum / minimum);
        commit (valueAtDragStart * std::exp (fraction * decades));
        return;
    }

    commit (valueAtDragStart + fraction * (maximum - minimum));
}

void DewNumberField::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void DewNumberField::mouseWheelMove (const juce::MouseEvent& event,
                                     const juce::MouseWheelDetails& wheel)
{
    // Read through the shared seam, so a field follows natural scrolling like
    // everything else does: with the Mac default on, a swipe that scrolled a
    // timeline forwards used to step this field backwards.
    const auto delta = gesture::deltaOf (wheel);

    if (juce::exactlyEqual (delta.y, 0.0))
        return;

    if (onEditStart != nullptr)
        onEditStart();

    const auto scale = gesture::isFine (event.mods) ? gesture::fineMultiplier : 1.0;
    const auto up = delta.y > 0.0;

    if (logarithmic && minimum > 0.0 && maximum > minimum)
    {
        // One notch is a fixed proportion of the range, in ratio terms.
        const auto ratio = std::exp (std::log (maximum / minimum) * 0.02
                                     * juce::jmax (1.0, scale * 4.0));
        commit (up ? value * ratio : value / ratio);
        return;
    }

    const auto step = interval > 0.0 ? interval : (maximum - minimum) / 100.0;
    commit (value + (up ? step : -step) * juce::jmax (1.0, scale * 4.0));
}

bool DewNumberField::keyPressed (const juce::KeyPress& key)
{
    // While a value is being typed the arrows belong to the caret. The editor
    // is a child and holds the focus, so it sees them first anyway - this is
    // the belt to that pair of braces, and it is what makes the rule readable.
    if (typed.isActive())
        return false;

    const auto command = keys::valueKeys::commandFor (key);
    const auto direction = keys::valueKeys::directionOf (command);

    if (direction == 0)
        return false;

    // Per step, exactly as mouseWheelMove does, and for a reason that is not
    // cosmetic: a field has no onEditEnd, so the flag its owner keeps for it
    // stays set once the first value has been written. Re-arming on every press
    // is what makes each press its own undo step; without it every later edit
    // in that field coalesces into one transaction for ever.
    if (onEditStart != nullptr)
        onEditStart();

    const auto fraction = keys::valueKeys::fractionFor (command, key.getModifiers(), interval);

    commit (keys::valueKeys::steppedValue (
        value, fraction, direction, interval,
        [this] (double v) { return normalisedOf (v, logarithmic, minimum, maximum); },
        [this] (double p) { return valueOf (p, logarithmic, minimum, maximum); }));

    return true;
}

void DewNumberField::mouseDoubleClick (const juce::MouseEvent&)
{
    beginTypedEdit();
}

void DewNumberField::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void DewNumberField::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void DewNumberField::beginTypedEdit()
{
    if (typed.isActive())
        return;

    if (onEditStart != nullptr)
        onEditStart();

    typed.begin (getLocalBounds().reduced (space::xxs), value, decimalPlaces, functionColour,
                 [this] (double newValue) { commit (newValue); });
}

void DewNumberField::resized()
{
    typed.setBounds (getLocalBounds().reduced (space::xxs));
}

void DewNumberField::paint (juce::Graphics& g)
{
    if (typed.isActive())
        return;

    auto bounds = paint::bodyRect (*this);

    paint::inputBox (g, *this, hovered || dragging ? colour::surfaceHover : colour::surfaceRaised,
                     dragging ? functionColour : colour::outline);

    auto text = bounds.toNearestInt();

    if (caption.isNotEmpty())
    {
        auto captionArea = text.removeFromTop (size::captionBand);
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::caption));
        g.drawText (caption, captionArea, juce::Justification::centred);
    }

    g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
    g.setFont (type::font (type::body));
    g.drawText (displayText(), text, juce::Justification::centred);

    // A drag affordance: two chevrons, only while the cursor is over the field.
    if (hovered && ! dragging)
    {
        g.setColour (colour::textDisabled);
        const auto right = (float) getWidth() - 7.0f;
        const auto mid = (float) getHeight() * 0.5f;

        juce::Path arrows;
        arrows.addTriangle (right - 3.0f, mid - 2.0f, right + 3.0f, mid - 2.0f, right, mid - 6.0f);
        arrows.addTriangle (right - 3.0f, mid + 2.0f, right + 3.0f, mid + 2.0f, right, mid + 6.0f);
        g.fillPath (arrows);
    }

    paint::focusRing (g, *this, focus::ringVisibleFor (hasKeyboardFocus (true)));
}

} // namespace dew
