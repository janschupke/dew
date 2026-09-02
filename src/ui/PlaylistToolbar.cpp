#include "ui/PlaylistToolbar.h"

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
        button.setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    };

    addTool (selectButton, PlaylistTool::select);
    addTool (paintButton, PlaylistTool::paint);

    const auto addZoom = [this] (DewIconButton& button, double factor)
    {
        button.setWantsKeyboardFocus (false);
        button.onClick = [this, factor] { if (onZoom) onZoom (factor); };
        addAndMakeVisible (button);
    };

    // The same factors the +/- keys use, and 0 for "fit the song".
    addZoom (zoomOutButton, 1.0 / 1.5);
    addZoom (zoomInButton, 1.5);
    addZoom (zoomFitButton, 0.0);

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

    auto area = getLocalBounds().reduced (space::md, space::xs);
    const auto controlHeight = juce::jmin (size::controlHeight, area.getHeight());

    const auto place = [&area, controlHeight] (juce::Component& c, int width)
    {
        c.setBounds (area.removeFromLeft (width).withHeight (controlHeight));
        area.removeFromLeft (space::xxs);
    };

    const auto divider = [this, &area]
    {
        area.removeFromLeft (space::sm);
        groupDividers.add (area.getX());
        area.removeFromLeft (space::sm + space::xs);
    };

    place (selectButton, size::iconButton);
    place (paintButton, size::iconButton);

    divider();

    place (zoomOutButton, size::iconButton);
    place (zoomInButton, size::iconButton);
    place (zoomFitButton, size::iconButton);
}

} // namespace dew
