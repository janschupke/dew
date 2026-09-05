#include "ui/PianoRollToolbar.h"

#include "i18n/Strings.h"
#include "ui/StripLayout.h"
#include "ui/design/Tokens.h"

namespace dew
{

PianoRollToolbar::PianoRollToolbar()
{
    setComponentID ("pianoRollToolbar");

    const auto addTool = [this] (DewIconButton& button, RollTool which)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, which] { setTool (which); };

        // A CLICK must not move focus off the roll, or the shortcuts it owns -
        // the arrows, the digits - stop working until the grid is clicked
        // again. That is this call, and only this call: refusing focus
        // outright also took every toolbar button out of the tab order, which
        // is the one way a keyboard reaches them at all.
        button.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (button);
    };

    addTool (selectButton, RollTool::select);
    addTool (paintButton, RollTool::paint);
    addTool (sliceButton, RollTool::slice);

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

    styleCaption (snapCaption, "SNAP");
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

    styleCaption (channelCaption, "CHANNEL");
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

    updateToolButtons();
}

void PianoRollToolbar::setTool (RollTool newTool, juce::NotificationType notification)
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

void PianoRollToolbar::updateToolButtons()
{
    selectButton.setToggleState (tool == RollTool::select, juce::dontSendNotification);
    paintButton.setToggleState (tool == RollTool::paint, juce::dontSendNotification);
    sliceButton.setToggleState (tool == RollTool::slice, juce::dontSendNotification);
}

void PianoRollToolbar::rebuildSnapBox()
{
    // Guarded because clear() fires onChange, which would read a selected id of
    // 0 back as "sixteenth" and quietly reset the user's grid on every refresh.
    const juce::ScopedValueSetter<bool> guard (updatingSnapBox, true);

    snapBox.clear (juce::dontSendNotification);

    for (int i = 0; i < NoteTools::numSnapDivisions; ++i)
        snapBox.addItem (NoteTools::nameForSnap (NoteTools::allSnapDivisions[i], beatUnit), i + 1);

    snapBox.setSelectedId (NoteTools::indexOfSnap (snap) + 1, juce::dontSendNotification);
}

void PianoRollToolbar::setBeatUnit (int newBeatUnit)
{
    if (newBeatUnit == beatUnit)
        return;

    beatUnit = newBeatUnit;
    rebuildSnapBox();
}

void PianoRollToolbar::paint (juce::Graphics& g)
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
    constexpr auto controls = size::iconButton * 7 + captionWidth * 2 + channelBoxWidth
                              + snapBoxWidth + 34 * 2 + ZoomButtons::preferredWidth
                              + VerticalZoomButtons::preferredWidth;

    constexpr auto steps = space::xxs * 15;
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

    place (selectButton, size::iconButton);
    place (paintButton, size::iconButton);
    place (sliceButton, size::iconButton);

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
