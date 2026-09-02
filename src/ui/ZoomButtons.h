#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/design/Icons.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

/** Zoom out, zoom in, fit - the group every timeline view offers.

    Declared twice before, in the two editor toolbars, and absent from the
    sequencer entirely: the step grid sized itself to its pattern and there was
    no way to say otherwise, so a 128-step pattern was hairlines and a 4-step
    one was four enormous cells.

    A component rather than three loose buttons, because the group has a shape -
    out, in, fit, in that order - and a toolbar that placed them in a different
    order would be a third opinion about what zoom looks like.
*/
class ZoomButtons : public juce::Component
{
public:
    /** @param fitTooltip  what "fit" fits, which is the only thing that differs
                           between the views: a song, a pattern, a channel. */
    explicit ZoomButtons (const juce::String& fitTooltip)
    {
        zoomFitButton.setTooltip (fitTooltip);

        const auto wire = [this] (DewIconButton& button, double factor)
        {
            button.setWantsKeyboardFocus (false);
            button.onClick = [this, factor] { if (onZoom) onZoom (factor); };
            addAndMakeVisible (button);
        };

        // The same factors the +/- keys use, and 0 for "fit".
        wire (zoomOutButton, 1.0 / zoomFactor);
        wire (zoomInButton, zoomFactor);
        wire (zoomFitButton, 0.0);
    }

    /** How hard one press zooms. Shared with the keyboard so a key and a button
        are the same gesture. */
    static constexpr double zoomFactor = 1.5;

    /** A factor to multiply by, or 0 meaning "fit the content". */
    std::function<void (double factor)> onZoom;

    static constexpr int buttonSize = tokens::size::iconButton;
    static constexpr int preferredWidth = buttonSize * 3 + tokens::space::xxs * 2;

    void resized() override
    {
        auto area = getLocalBounds();
        const auto size = juce::jmin (buttonSize, area.getHeight());

        for (auto* button : { &zoomOutButton, &zoomInButton, &zoomFitButton })
        {
            button->setBounds (area.removeFromLeft (buttonSize).withHeight (size));
            area.removeFromLeft (tokens::space::xxs);
        }
    }

private:
    DewIconButton zoomOutButton { icons::zoomOut(), "Zoom out (-)" };
    DewIconButton zoomInButton { icons::zoomIn(), "Zoom in (+)" };
    DewIconButton zoomFitButton { icons::fitToContent(), "Fit to the window (0)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoomButtons)
};

} // namespace dew
