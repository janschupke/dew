#include "ui/primitives/DewNumberField.h"

#include "ui/design/Gestures.h"
#include "ui/design/Cursors.h"
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
}

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
    if (editor != nullptr)
        return;

    if (onEditStart != nullptr)
        onEditStart();

    editor = std::make_unique<juce::TextEditor>();
    editor->setBounds (getLocalBounds().reduced (space::xxs));
    editor->setJustification (juce::Justification::centred);
    editor->setFont (type::font (type::body));
    editor->setText (juce::String (value, decimalPlaces), false);
    editor->setSelectAllWhenFocused (true);
    editor->setColour (juce::TextEditor::backgroundColourId, colour::well);
    editor->setColour (juce::TextEditor::textColourId, colour::textPrimary);
    editor->setColour (juce::TextEditor::outlineColourId, functionColour);

    const auto finish = [this] (bool keep)
    {
        if (editor == nullptr)
            return;

        const auto typed = editor->getText();
        editor.reset();

        if (keep && typed.isNotEmpty())
            commit (typed.getDoubleValue());

        repaint();
    };

    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };

    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void DewNumberField::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (space::xxs));
}

void DewNumberField::paint (juce::Graphics& g)
{
    if (editor != nullptr)
        return;

    auto bounds = paint::bodyRect (*this);

    g.setColour (hovered || dragging ? colour::surfaceHover : colour::surfaceRaised);
    g.fillRoundedRectangle (bounds, radius::sm);

    g.setColour (dragging ? functionColour : colour::outline);
    g.drawRoundedRectangle (bounds, radius::sm, stroke::hairline);

    auto text = bounds.toNearestInt();

    if (caption.isNotEmpty())
    {
        auto captionArea = text.removeFromTop (12);
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
