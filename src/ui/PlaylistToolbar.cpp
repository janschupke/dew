#include "ui/PlaylistToolbar.h"

#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

PlaylistToolbar::PlaylistToolbar()
{
    setComponentID ("playlistToolbar");

    const auto addTool = [this] (DewIconButton& button, PlaylistTool which)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, which] { setTool (which); };

        // Without this a click on a tool moves focus off the arrangement, and
        // the shortcuts it owns stop working until a lane is clicked again.
        button.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (button);
    };

    addTool (selectButton, PlaylistTool::select);
    addTool (paintButton, PlaylistTool::paint);

    zoomButtons.onZoom = [this] (double factor)
    {
        if (onZoom)
            onZoom (factor);
    };
    addAndMakeVisible (zoomButtons);

    heightButtons.onHeightChange = [this] (double factor)
    {
        if (onTrackHeight)
            onTrackHeight (factor);
    };
    addAndMakeVisible (heightButtons);

    updateToolButtons();
}

void PlaylistToolbar::setTool (PlaylistTool newTool, juce::NotificationType notification)
{
    if (tool == newTool)
    {
        // The radio group can clear the button of the tool that is already
        // current when it is clicked again; put it back rather than leaving the
        // strip showing no tool at all.
        updateToolButtons();
        return;
    }

    tool = newTool;
    updateToolButtons();

    if (notification != juce::dontSendNotification && onToolChanged)
        onToolChanged();
}

void PlaylistToolbar::updateToolButtons()
{
    selectButton.setToggleState (tool == PlaylistTool::select, juce::dontSendNotification);
    paintButton.setToggleState (tool == PlaylistTool::paint, juce::dontSendNotification);
}

void PlaylistToolbar::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::surface);

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    for (const auto x : groupDividers)
    {
        g.setColour (colour::divider);
        g.drawVerticalLine (x, 7.0f, (float) getHeight() - 7.0f);
    }
}

void PlaylistToolbar::resized()
{
    using namespace tokens;

    groupDividers.clear();

    StripLayout strip { getLocalBounds(), space::md, space::xs };

    const auto place = [&strip] (juce::Component& c, int width) { strip.place (c, width); };
    const auto divider = [this, &strip] { groupDividers.add (strip.divider()); };

    place (selectButton, size::iconButton);
    place (paintButton, size::iconButton);

    divider();

    place (zoomButtons, ZoomButtons::preferredWidth);

    // Its own group: the two are the same gesture on different axes, and running
    // them together would read as one six-button zoom.
    divider();

    place (heightButtons, VerticalZoomButtons::preferredWidth);
}

} // namespace dew
