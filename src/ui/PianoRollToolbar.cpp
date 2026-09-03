#include "ui/PianoRollToolbar.h"

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

        // Without this a click on a tool moves focus off the roll, and the
        // shortcuts it owns - the arrows, the digits - stop working until the
        // grid is clicked again.
        button.setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    };

    addTool (selectButton, RollTool::select);
    addTool (paintButton, RollTool::paint);
    addTool (sliceButton, RollTool::slice);

    rebuildSnapBox();
    snapBox.setTooltip ("Grid the editing gestures snap to");
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        if (updatingSnapBox)
            return;

        setSnap (NoteTools::snapFromIndex (snapBox.getSelectedId() - 1));
    };
    addAndMakeVisible (snapBox);

    channelBox.setTooltip ("Channel being edited");
    channelBox.setWantsKeyboardFocus (false);
    channelBox.onChange = [this]
    {
        if (updatingChannelBox)
            return;

        if (onChannelChanged && channelBox.getSelectedId() > 0)
            onChannelChanged (channelBox.getSelectedId());
    };
    addAndMakeVisible (channelBox);

    // Zoom-to-fit used to be reachable only by double-clicking the piano keys,
    // which is also where a double-click means "audition this twice".
    zoomButtons.onZoom = [this] (double factor) { if (onZoom) onZoom (factor); };
    addAndMakeVisible (zoomButtons);

    rowHeightButtons.onHeightChange = [this] (double factor)
    {
        if (onRowHeight)
            onRowHeight (factor);
    };

    addAndMakeVisible (rowHeightButtons);

    const auto addAction = [this] (juce::Button& button, std::function<void()>& callback)
    {
        button.setWantsKeyboardFocus (false);
        button.onClick = [&callback] { if (callback) callback(); };
        addAndMakeVisible (button);
    };

    addAction (quantizeButton, onQuantize);
    addAction (randomizeButton, onRandomize);

    const auto addTranspose = [this] (juce::Button& button, int semitones)
    {
        button.setWantsKeyboardFocus (false);
        button.onClick = [this, semitones] { if (onTranspose) onTranspose (semitones); };
        addAndMakeVisible (button);
    };

    addTranspose (upButton, 1);
    addTranspose (downButton, -1);
    addTranspose (octaveUpButton, 12);
    addTranspose (octaveDownButton, -12);

    octaveUpButton.setTooltip ("Up an octave (Shift+Up)");
    octaveDownButton.setTooltip ("Down an octave (Shift+Down)");

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

void PianoRollToolbar::resized()
{
    using namespace tokens;

    groupDividers.clear();

    StripLayout strip { getLocalBounds(), space::md, space::xs };

    const auto place = [&strip] (juce::Component& c, int width) { strip.place (c, width); };
    const auto divider = [this, &strip] { groupDividers.add (strip.divider()); };

    place (selectButton, size::iconButton);
    place (paintButton, size::iconButton);
    place (sliceButton, size::iconButton);

    divider();

    place (channelBox, 132);

    divider();

    place (snapBox, 78);

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
