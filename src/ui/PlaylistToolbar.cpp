#include "ui/primitives/DewPaint.h"
#include "ui/PlaylistToolbar.h"

#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

// The roll's own numbers, restated here rather than shared: the two strips are
// different lengths and the floors are about what a squeezed dropdown has to
// keep, which is a fact about the control and not about the strip.
constexpr int snapBoxWidth = 78;
constexpr int snapBoxMinWidth = tokens::size::iconButton * 2 + tokens::space::sm;
constexpr int captionWidth = tokens::size::gutterLabel - tokens::space::lg;

} // namespace

PlaylistToolbar::PlaylistToolbar()
{
    setComponentID ("playlistToolbar");

    tools.onToolChanged = [this]
    {
        if (onToolChanged)
            onToolChanged();
    };

    rebuildSnapBox();
    snapBox.setTooltip (tr (StringId::playlist_snap_help));
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        if (updatingSnapBox)
            return;

        setSnap (NoteTools::snapFromIndex (snapBox.getSelectedId() - 1));
    };
    addAndMakeVisible (snapBox);

    styleCaption (snapCaption, StringId::playlist_snap_caption);
    addAndMakeVisible (snapCaption);

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

void PlaylistToolbar::setSnap (SnapDivision newSnap, juce::NotificationType notification)
{
    if (snap == newSnap)
        return;

    snap = newSnap;
    rebuildSnapBox();

    if (notification != juce::dontSendNotification && onSnapChanged)
        onSnapChanged();
}

void PlaylistToolbar::setGrid (int newStepsPerBeat, int newBeatsPerBar, int newBeatUnit)
{
    if (newBeatUnit == beatUnit && newStepsPerBeat == stepsPerBeat && newBeatsPerBar == beatsPerBar)
        return;

    beatUnit = newBeatUnit;
    stepsPerBeat = newStepsPerBeat;
    beatsPerBar = newBeatsPerBar;

    setSnap (NoteTools::nearestFittingSnap (snap, stepsPerBeat, beatsPerBar));
    rebuildSnapBox();
}

void PlaylistToolbar::rebuildSnapBox()
{
    // Guarded because clear() fires onChange, which would read a selected id of
    // 0 back as the finest division and quietly reset the grid on every
    // refresh - the piano roll's own note, and the same bug.
    const juce::ScopedValueSetter<bool> guard (updatingSnapBox, true);

    snapBox.clear (juce::dontSendNotification);

    for (int i = 0; i < NoteTools::numSnapDivisions; ++i)
    {
        const auto division = NoteTools::allSnapDivisions[i];
        snapBox.addItem (NoteTools::nameForSnap (division, beatUnit), i + 1);

        if (! NoteTools::fitsGrid (division, stepsPerBeat, beatsPerBar))
            snapBox.setItemEnabled (i + 1, false);
    }

    snapBox.setSelectedId (NoteTools::indexOfSnap (snap) + 1, juce::dontSendNotification);
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
    const auto controls = tools.preferredWidth() + captionWidth + snapBoxWidth
                          + ZoomButtons::preferredWidth + VerticalZoomButtons::preferredWidth;

    constexpr auto steps = space::xxs * 4;
    constexpr auto dividers = (space::sm * 2 + space::xs) * 3;

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

    // A caption and its dropdown go together or not at all, as in the roll: a
    // word with nothing beside it says less than no word.
    if (strip.getRemainingWidth() >= captionWidth + snapBoxMinWidth
        && strip.place (snapCaption, captionWidth, 0)
        && strip.place (snapBox, snapBoxWidth, snapBoxMinWidth))
    {
        divider();
    }
    else
    {
        snapCaption.setVisible (false);
        snapBox.setVisible (false);
        overflow.add (snapBox);
    }

    place (zoomButtons, ZoomButtons::preferredWidth);

    // Its own group: the two are the same gesture on different axes, and running
    // them together would read as one six-button zoom.
    divider();

    place (heightButtons, VerticalZoomButtons::preferredWidth);
}

} // namespace dew
