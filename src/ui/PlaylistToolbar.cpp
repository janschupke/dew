#include "ui/primitives/DewPaint.h"
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

    overflowButton.setComponentID ("toolbarOverflow");
    overflowButton.setMouseClickGrabsKeyboardFocus (false);
    overflowButton.setVisible (false);
    overflowButton.onClick = [this]
    {
        auto menu = getOverflowMenu();
        menu.setLookAndFeel (&getLookAndFeel());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&overflowButton),
                            [this] (int choice) { applyOverflowChoice (choice); });
    };
    addChildComponent (overflowButton);

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
    paint::toolbarStrip (g, *this, groupDividers);
}

int PlaylistToolbar::preferredWidth() const
{
    using namespace tokens;

    constexpr auto controls = size::iconButton * 2 + ZoomButtons::preferredWidth
                              + VerticalZoomButtons::preferredWidth;

    constexpr auto steps = space::xxs * 4;
    constexpr auto dividers = (space::sm * 2 + space::xs) * 2;

    return controls + steps + dividers + space::md * 2;
}

void PlaylistToolbar::resized()
{
    using namespace tokens;

    groupDividers.clear();
    overflow.clear();

    StripLayout strip { getLocalBounds(), space::md, space::xs };

    // Reserved before anything is placed: a strip that discovers it needs the
    // button after it has run out has nowhere left to put it.
    const auto overflowing = getWidth() < preferredWidth();

    overflowButton.setVisible (overflowing);

    if (overflowing)
        strip.placeAtEnd (overflowButton, size::iconButton);

    const auto place = [this, &strip] (juce::Component& c, int width)
    {
        if (! strip.place (c, width))
            overflow.add (c);
    };

    const auto divider = [this, &strip]
    {
        if (strip.getRemainingWidth() > 0)
            groupDividers.add (strip.divider());
    };

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
