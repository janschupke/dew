#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ZoomButtons.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** Taller, shorter, fit - ONE control for the height of every playlist lane.

    Not per-track. A per-track height sounds more flexible and is not: an
    automation curve is only editable at a height somebody chose for it, and a
    view where that is true of some lanes and not others is a view where the same
    gesture works or does not depending on where you aim it.

    Buttons rather than a drag handle on the header's edge, for three reasons.
    Discrete geometric steps mean the header has to look right at five heights
    rather than at every height between the ends of the range, which is the
    difference between adding a control and redesigning a header. The group is
    ZoomButtons one axis over, so a toolbar holding both states one idea twice
    rather than two ideas once. And the seam a test drives is the seam the user
    reaches, which is what `onHeightChange` being the same shape as `onZoom`
    buys.

    Chevrons rather than a second magnifier pair: two of those in one 34px strip
    is unreadable, and the axis is what distinguishes them.
*/
class TrackHeightButtons : public juce::Component
{
public:
    TrackHeightButtons()
    {
        const auto wire = [this] (DewIconButton& button, double factor)
        {
            button.setWantsKeyboardFocus (false);
            button.onClick = [this, factor] { if (onHeightChange) onHeightChange (factor); };
            addAndMakeVisible (button);
        };

        wire (shorterButton, 1.0 / heightFactor);
        wire (tallerButton, heightFactor);
        wire (fitButton, 0.0);
    }

    /** The same factor a zoom press uses, deliberately: one press means the same
        amount of change on either axis. */
    static constexpr double heightFactor = ZoomButtons::zoomFactor;

    /** A factor to multiply the lane height by, or 0 meaning "fit the tracks to
        the window" - the shape ZoomButtons reports, so one toolbar holding both
        groups states one idea on two axes. */
    std::function<void (double factor)> onHeightChange;

    static constexpr int buttonSize = tokens::size::iconButton;
    static constexpr int preferredWidth = buttonSize * 3 + tokens::space::xxs * 2;

    void resized() override
    {
        auto area = getLocalBounds();
        const auto size = juce::jmin (buttonSize, area.getHeight());

        for (auto* button : { &shorterButton, &tallerButton, &fitButton })
        {
            button->setBounds (area.removeFromLeft (buttonSize).withHeight (size));
            area.removeFromLeft (tokens::space::xxs);
        }
    }

private:
    DewIconButton shorterButton { icons::chevronUp(), "Shorter tracks" };
    DewIconButton tallerButton { icons::chevronDown(), "Taller tracks" };
    DewIconButton fitButton { icons::fitToContent(), "Fit the tracks to the window" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeightButtons)
};

} // namespace dew
