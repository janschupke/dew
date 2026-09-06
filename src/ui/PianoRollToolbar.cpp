#include "ui/primitives/DewPaint.h"
#include "ui/PianoRollToolbar.h"

#include "i18n/Strings.h"
#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

PianoRollToolbar::PianoRollToolbar()
{
    setComponentID ("pianoRollToolbar");

    tools.onToolChanged = [this]
    {
        if (onToolChanged)
            onToolChanged();
    };

    rebuildSnapBox();
    snapBox.setTooltip (tr (StringId::pianoRoll_snap_help));
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        if (updatingSnapBox)
            return;

        setSnap (NoteTools::snapFromIndex (snapBox.getSelectedId() - 1));
    };
    addAndMakeVisible (snapBox);

    styleCaption (snapCaption, StringId::pianoRoll_snap_caption);
    addAndMakeVisible (snapCaption);

    channelBox.setTooltip (tr (StringId::pianoRoll_channel_help));
    channelBox.setMouseClickGrabsKeyboardFocus (false);
    channelBox.onChange = [this]
    {
        if (updatingChannelBox)
            return;

        if (onChannelChanged && channelBox.getSelectedId() > 0)
            onChannelChanged (channelBox.getSelectedId());
    };
    addAndMakeVisible (channelBox);

    styleCaption (channelCaption, StringId::pianoRoll_channel_caption);
    addAndMakeVisible (channelCaption);

    // Zoom-to-fit used to be reachable only by double-clicking the piano keys,
    // which is also where a double-click means "audition this twice".
    zoomButtons.onZoom = [this] (double factor)
    {
        if (onZoom)
            onZoom (factor);
    };
    addAndMakeVisible (zoomButtons);

    rowHeightButtons.onHeightChange = [this] (double factor)
    {
        if (onRowHeight)
            onRowHeight (factor);
    };

    addAndMakeVisible (rowHeightButtons);

    const auto addAction = [this] (juce::Button& button, std::function<void()>& callback)
    {
        button.setMouseClickGrabsKeyboardFocus (false);
        button.onClick = [&callback]
        {
            if (callback)
                callback();
        };
        addAndMakeVisible (button);
    };

    addAction (quantizeButton, onQuantize);
    addAction (randomizeButton, onRandomize);

    const auto addTranspose = [this] (juce::Button& button, int semitones)
    {
        button.setMouseClickGrabsKeyboardFocus (false);
        button.onClick = [this, semitones]
        {
            if (onTranspose)
                onTranspose (semitones);
        };
        addAndMakeVisible (button);
    };

    addTranspose (upButton, 1);
    addTranspose (downButton, -1);
    addTranspose (octaveUpButton, 12);
    addTranspose (octaveDownButton, -12);

    octaveUpButton.setTooltip (tr (StringId::pianoRoll_octaveUp_help));
    octaveDownButton.setTooltip (tr (StringId::pianoRoll_octaveDown_help));

    overflowButton.setComponentID ("toolbarOverflow");
    overflowButton.setMouseClickGrabsKeyboardFocus (false);
    overflowButton.setVisible (false);
    overflowButton.onClick = [this]
    {
        auto menu = buildOverflowMenu();
        menu.setLookAndFeel (&getLookAndFeel());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&overflowButton),
                            [this] (int choice) { applyOverflowChoice (choice); });
    };
    addChildComponent (overflowButton);
}

void PianoRollToolbar::setSnap (SnapDivision newSnap, juce::NotificationType notification)
{
    if (snap == newSnap)
        return;

    snap = newSnap;

    {
        const juce::ScopedValueSetter<bool> quiet (updatingSnapBox, true);
        snapBox.setSelectedId (NoteTools::indexOfSnap (snap) + 1, juce::dontSendNotification);
    }

    if (notification != juce::dontSendNotification && onSnapChanged)
        onSnapChanged();
}

void PianoRollToolbar::setChannels (const juce::StringArray& names, const juce::Array<int>& ids)
{
    jassert (names.size() == ids.size());

    const auto wanted = channelBox.getSelectedId();

    const juce::ScopedValueSetter<bool> quiet (updatingChannelBox, true);
    channelBox.clear (juce::dontSendNotification);

    for (int i = 0; i < ids.size(); ++i)
        channelBox.addItem (names[i], ids[i]);

    // Channel ids are the item ids, so a rebuild that still holds the current
    // channel keeps it selected rather than jumping to the first row.
    channelBox.setSelectedId (wanted, juce::dontSendNotification);
}

void PianoRollToolbar::setSelectedChannel (int channelId)
{
    if (channelBox.getSelectedId() == channelId)
        return;

    const juce::ScopedValueSetter<bool> quiet (updatingChannelBox, true);
    channelBox.setSelectedId (channelId, juce::dontSendNotification);
}

int PianoRollToolbar::getSelectedChannel() const noexcept
{
    return channelBox.getSelectedId();
}

void PianoRollToolbar::rebuildSnapBox()
{
    // Guarded because clear() fires onChange, which would read a selected id of
    // 0 back as "sixteenth" and quietly reset the user's grid on every refresh.
    const juce::ScopedValueSetter<bool> guard (updatingSnapBox, true);

    snapBox.clear (juce::dontSendNotification);

    for (int i = 0; i < NoteTools::numSnapDivisions; ++i)
    {
        const auto division = NoteTools::allSnapDivisions[i];
        snapBox.addItem (NoteTools::nameForSnap (division, beatUnit), i + 1);

        // OFFERED and disabled rather than absent. A division this grid cannot
        // express is a real division that this project cannot use, and a list
        // that simply left it out would say the application does not have
        // triplets rather than that the grid does not - which is the difference
        // between a missing feature and a setting to change.
        if (! NoteTools::fitsGrid (division, stepsPerBeat, beatsPerBar))
            snapBox.setItemEnabled (i + 1, false);
    }

    snapBox.setSelectedId (NoteTools::indexOfSnap (snap) + 1, juce::dontSendNotification);
}

void PianoRollToolbar::setGrid (int newStepsPerBeat, int newBeatsPerBar, int newBeatUnit)
{
    if (newBeatUnit == beatUnit && newStepsPerBeat == stepsPerBeat && newBeatsPerBar == beatsPerBar)
        return;

    beatUnit = newBeatUnit;
    stepsPerBeat = newStepsPerBeat;
    beatsPerBar = newBeatsPerBar;

    // A division the new grid cannot express stops being selected. Leaving it
    // would be a dropdown showing a setting that is not in force - the snap
    // would quietly behave as the identity while the box still named a
    // sixteenth.
    setSnap (NoteTools::nearestFittingSnap (snap, stepsPerBeat, beatsPerBar));

    rebuildSnapBox();
}

void PianoRollToolbar::paint (juce::Graphics& g)
{
    paint::toolbarStrip (g, *this, groupDividers);
}

namespace
{

// How wide the two dropdowns like to be, and how narrow they will go. A channel
// name and a fraction both stay readable well below their comfortable width,
// which is what makes them the right things to squeeze before anything is
// dropped: a button has no narrower version of itself.
constexpr int channelBoxWidth = 132;
constexpr int snapBoxWidth = 78;

// The floors are multiples of the ladder's own rungs rather than numbers
// chosen by eye, so a change to the rung carries them: enough for a couple of
// characters and the chevron, which is what a squeezed dropdown has to keep.
constexpr int channelBoxMinWidth = tokens::size::iconButton * 3;
constexpr int snapBoxMinWidth = tokens::size::iconButton * 2 + tokens::space::sm;

// Wide enough for the longest of them - one width, so the two dropdowns start
// at the same offset from their divider rather than at two.
constexpr int captionWidth = tokens::size::gutterLabel - tokens::space::lg;

} // namespace

int PianoRollToolbar::preferredWidth() const
{
    using namespace tokens;

    // Everything, at the width it likes, plus the step after each control and
    // the six group rules. Written as a sum rather than measured after the fact
    // because resized() has to know BEFORE it starts whether to reserve the
    // overflow button's slot.
    // The tools ask for their own width - see ToolStrip::preferredWidth - so
    // adding one to the register is not also a number to remember here.
    const auto controls = tools.preferredWidth() + size::iconButton * 4 + captionWidth * 2
                          + channelBoxWidth + snapBoxWidth + 34 * 2 + ZoomButtons::preferredWidth
                          + VerticalZoomButtons::preferredWidth;

    constexpr auto steps = space::xxs * 13;
    constexpr auto dividers = (space::sm * 2 + space::xs) * 6;

    return controls + steps + dividers + space::md * 2;
}

void PianoRollToolbar::resized()
{
    using namespace tokens;

    groupDividers.clear();
    overflow.clear();

    StripLayout strip { getLocalBounds(), space::md, space::xs };

    // Reserved BEFORE anything is placed, because a strip that discovers it
    // needs the button after it has run out has nowhere left to put it.
    const auto overflowing = getWidth() < preferredWidth();

    overflowButton.setVisible (overflowing);

    if (overflowing)
        strip.placeAtEnd (overflowButton, size::iconButton);

    // Placed in the order they are read, and DROPPED in the reverse of the
    // order they matter - which is why the least important groups are last.
    // The tools and the two dropdowns are the strip; the rest is reachable from
    // the keyboard as well, and from the >> menu when it is not here.
    const auto place = [this, &strip] (juce::Component& c, int width, int minimum = -1)
    {
        if (! strip.place (c, width, minimum))
            overflow.add (c);
    };

    // A caption and its dropdown go together or not at all: a word with nothing
    // beside it says less than no word, and the caption is not offered in the
    // menu because the dropdown's own label already says what it is.
    const auto placeLabelled =
        [this, &strip] (juce::Component& caption, juce::Component& box, int width, int minimum)
    {
        const auto both = strip.getRemainingWidth() >= captionWidth + minimum;

        if (both && strip.place (caption, captionWidth, 0) && strip.place (box, width, minimum))
            return;

        caption.setVisible (false);
        box.setVisible (false);
        overflow.add (box);
    };

    // A rule with nothing after it is a rule at the end of the strip. Only
    // recorded while there is still something to separate.
    const auto divider = [this, &strip]
    {
        if (strip.getRemainingWidth() > 0)
            groupDividers.add (strip.divider());
    };

    tools.placeAll (place);

    divider();

    placeLabelled (channelCaption, channelBox, channelBoxWidth, channelBoxMinWidth);

    divider();

    placeLabelled (snapCaption, snapBox, snapBoxWidth, snapBoxMinWidth);

    divider();

    place (quantizeButton, size::iconButton);
    place (randomizeButton, size::iconButton);

    divider();

    place (downButton, size::iconButton);
    place (upButton, size::iconButton);
    place (octaveDownButton, 34);
    place (octaveUpButton, 34);

    divider();

    place (zoomButtons, ZoomButtons::preferredWidth);

    // Its own group, as in the playlist: the two are the same gesture on
    // different axes, and running them together reads as one six-button zoom.
    divider();

    place (rowHeightButtons, VerticalZoomButtons::preferredWidth);
}

} // namespace dew
