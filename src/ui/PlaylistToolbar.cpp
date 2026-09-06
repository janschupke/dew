#include "ui/primitives/DewPaint.h"
#include "ui/PlaylistToolbar.h"

#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

PlaylistToolbar::PlaylistToolbar()
{
    setComponentID ("playlistToolbar");

    tools.onToolChanged = [this]
    {
        if (onToolChanged)
            onToolChanged();
    };

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
}

void PlaylistToolbar::paint (juce::Graphics& g)
{
    paint::toolbarStrip (g, *this, groupDividers);
}

int PlaylistToolbar::preferredWidth() const
{
    using namespace tokens;

    // The tools ask for their own width, so adding one to the register is not
    // also a number to remember here - which is what went quietly wrong before,
    // since a strip that under-reports its width never shows its overflow
    // button and simply drops the controls that did not fit.
    const auto controls = tools.preferredWidth() + ZoomButtons::preferredWidth
                          + VerticalZoomButtons::preferredWidth;

    constexpr auto steps = space::xxs * 2;
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

    tools.placeAll (place);

    divider();

    place (zoomButtons, ZoomButtons::preferredWidth);

    // Its own group: the two are the same gesture on different axes, and running
    // them together would read as one six-button zoom.
    divider();

    place (heightButtons, VerticalZoomButtons::preferredWidth);
}

} // namespace dew
