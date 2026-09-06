#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ZoomButtons.h"
#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewButtons.h"

namespace dew
{

/** Shorter, taller, fit - ZoomButtons one axis over.

    Zoom is horizontal in every timeline dew has, because time is. This is the
    group for the axis that is left: playlist lanes, and piano-roll pitch rows.
    It was TrackHeightButtons and served only the playlist; the roll wanted the
    same three buttons for the same reason, and two classes doing this would be
    two opinions about which order they go in.

    ONE height for every row, not one per row. A per-track height sounds more
    flexible and is not: an automation curve is only editable at a height
    somebody chose for it, and a view where that is true of some lanes and not
    others is a view where the same gesture works or does not depending on where
    you aim it.

    The buttons are not the only way in - alt-`=`, alt-`-` and alt-`0` reach the
    same three calls, and so does a drag on a track header's bottom edge. They
    are the DISCOVERABLE way in, which is what a toolbar is for: the drag has no
    affordance until the pointer is already on it, and a key has none at all.

    Its own icons rather than chevrons. Chevrons were fine while this only
    served the playlist; the piano roll's toolbar already spends a chevron pair
    on transposing a semitone, and two identical pairs meaning different things
    in one 34px strip is exactly the drift the design system exists to stop.
    The pair draws the CONTENT - more rules for denser rows, fewer for roomier -
    and fit is fitToContent turned through a right angle, because it is the same
    command on the other axis.
*/
class VerticalZoomButtons : public juce::Component
{
public:
    /** @param shorterTooltip  what gets shorter - a track, a row.
        @param tallerTooltip   the same thing, taller.
        @param fitTooltip      what "fit" fits, which is the only one that has
                               to name the material rather than the row.
    */
    VerticalZoomButtons (const juce::String& shorterTooltip, const juce::String& tallerTooltip,
                         const juce::String& fitTooltip)
    {
        shorterButton.setTooltip (shorterTooltip);
        tallerButton.setTooltip (tallerTooltip);
        fitButton.setTooltip (fitTooltip);

        const auto wire = [this] (DewIconButton& button, double factor)
        {
            button.setMouseClickGrabsKeyboardFocus (false);
            button.onClick = [this, factor]
            {
                if (onHeightChange)
                    onHeightChange (factor);
            };
            addAndMakeVisible (button);
        };

        wire (shorterButton, 1.0 / heightFactor);
        wire (tallerButton, heightFactor);
        wire (fitButton, 0.0);
    }

    /** The same factor a zoom press uses, deliberately: one press means the same
        amount of change on either axis. */
    static constexpr double heightFactor = ZoomButtons::zoomFactor;

    /** A factor to multiply the row height by, or 0 meaning "fit the content to
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
    DewIconButton shorterButton { icons::rowsShorter(), {} };
    DewIconButton tallerButton { icons::rowsTaller(), {} };
    DewIconButton fitButton { icons::fitRows(), {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerticalZoomButtons)
};

} // namespace dew
