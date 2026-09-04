#include "ui/ChannelRackHeader.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;

ChannelRackHeader::ChannelRackHeader (ProjectDocument& d, EditorState& s, juce::ValueTree c)
    : document (d)
    , editorState (s)
    , channel (std::move (c))
{
    // Named, so a test can find a row by asking rather than by counting the
    // widgets on it. It used to be identified as "one label and two
    // buttons", which stopped being true the moment a row gained a third.
    setComponentID ("channelHeader");

    nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
    nameLabel.setEditable (false, true, false);

    // The label covered the row's whole left half and consumed every press,
    // so clicking a channel by its name selected nothing. Renaming moves to
    // a double-click on the row, which is where it already was.
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (type::font (type::body));
    nameLabel.onTextChange = [this]
    {
        ProjectEdits::setProperty (channel, ids::name, nameLabel.getText(),
                                   &document.getUndoManager(), "Rename channel");
    };
    addAndMakeVisible (nameLabel);

    muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
    muteButton.onClick = [this]
    {
        // Selects on the CLICK. This was on onStateChange, which fires for
        // every internal transition - buttonNormal -> buttonOver among
        // them - and the row forwards its children's mouse events so it can
        // light up on hover. Between the two, merely moving the pointer
        // across M selected that channel with no click at all.
        select();

        ProjectEdits::setProperty (channel, ids::muted, muteButton.getToggleState(),
                                   &document.getUndoManager(), "Mute channel");
    };
    addAndMakeVisible (muteButton);

    soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
    soloButton.onClick = [this]
    {
        select();

        ProjectEdits::setProperty (channel, ids::solo, soloButton.getToggleState(),
                                   &document.getUndoManager(), "Solo channel");
    };
    addAndMakeVisible (soloButton);

    // Volume and pan on the row itself, so a pattern can be balanced without
    // selecting each channel in turn and reaching for the instrument panel.
    // Same ranges as the panel's VOLUME and PAN, so the two read the same
    // number, and no caption fits on a 34px row - pan is told from volume by
    // filling out from the centre.
    attachKnob (volumeKnob, ids::volume, "Change volume", "Volume");
    attachKnob (panKnob, ids::pan, "Change pan", "Pan");
    panKnob.setBipolar (true);

    // Only audio channels can be armed, and only one channel at a time -
    // clicking an armed row's R disarms it rather than arming a second.
    armButton.setTooltip ("Arm this channel for recording");
    armButton.onClick = [this]
    {
        select();
        editorState.setArmedChannelId (armButton.getToggleState() ? getChannelId() : 0);
    };
    addChildComponent (armButton);

    // The knobs and the arm button keep their own clicks too, and select
    // the row from their own handlers - see attachKnob.

    // Hover only - see forwardChildMouseEventsTo.
    forwardChildMouseEventsTo (*this);
    setMouseCursor (cursor::clickable);
}

void ChannelRackHeader::select()
{
    editorState.setSelectedChannelId (getChannelId());
}

void ChannelRackHeader::refresh()
{
    // Guarded because this runs on every property change of this channel,
    // including the knob's own write: without it a drag would feed its value
    // back into the slider it came from.
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    nameLabel.setText (channel[ids::name].toString(), juce::dontSendNotification);
    muteButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
    soloButton.setToggleState ((bool) channel[ids::solo], juce::dontSendNotification);
    volumeKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);

    const auto audio = ProjectEdits::playsClips (channel);
    armButton.setVisible (audio);
    armButton.setToggleState (audio && editorState.getArmedChannelId() == getChannelId(),
                              juce::dontSendNotification);

    resized();
    repaint();
}

// --- the menu ----------------------------------------------------------------

juce::PopupMenu ChannelRackHeader::buildMenu() const
{
    juce::PopupMenu menu;
    menu.addItem ((int) MenuItem::rename, "Rename");
    colourMenu::addTo (menu, channel, colourBaseId);
    menu.addItem ((int) MenuItem::addChannel, "Add channel");
    menu.addSeparator();
    menu.addItem ((int) MenuItem::removeChannel, "Remove channel");
    return menu;
}

void ChannelRackHeader::applyMenuChoice (int choice)
{
    if (colourMenu::apply (choice, channel, colourBaseId, document))
        return;

    switch ((MenuItem) choice)
    {
        case MenuItem::rename: nameLabel.showEditor(); break;
        case MenuItem::addChannel:
            if (onAddChannel)
                onAddChannel();
            break;
        case MenuItem::removeChannel:
            if (onRemoveChannel)
                onRemoveChannel (getChannelId());
            break;
        default: break;
    }
}

// --- the controls ------------------------------------------------------------

void ChannelRackHeader::attachKnob (DewKnob& knob, const juce::Identifier& property,
                                    const juce::String& transactionName,
                                    const juce::String& tooltip)
{
    knob.setCompact (true);
    knob.setTooltip (tooltip);
    knob.setValue ((double) channel[property], juce::dontSendNotification);

    knob.onEditStart = [this]
    {
        select();
        inDrag = true;
        gestureActive = false;
    };
    knob.onEditEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };

    knob.onValueChange = [this, &knob, property, transactionName]
    {
        if (updating)
            return;

        // The first value of a drag opens the transaction and the rest join
        // it; a wheel or keyboard change is not part of a drag and opens its
        // own. beginNewTransaction ARMS a new one rather than being a no-op
        // when one is open, which is why the distinction has to be made.
        ProjectEdits::setProperty (channel, property, knob.getValue(), &document.getUndoManager(),
                                   transactionName, gestureActive);

        gestureActive = inDrag;
    };

    addAndMakeVisible (knob);
}

void ChannelRackHeader::attachParamMenus (const paramMenu::Host* host)
{
    const auto self = [this] { return channel; };

    paramMenu::attachTo (host, volumeKnob, self, requireInstrumentParamSpec (ids::volume));
    paramMenu::attachTo (host, panKnob, self, requireInstrumentParamSpec (ids::pan));
    paramMenu::attachTo (host, muteButton, self, requireInstrumentParamSpec (ids::muted));
    paramMenu::attachTo (host, soloButton, self, requireInstrumentParamSpec (ids::solo));
}

// --- painting and layout -----------------------------------------------------

void ChannelRackHeader::paint (juce::Graphics& g)
{
    const auto selected = editorState.getSelectedChannelId() == getChannelId();

    // The rack's rest and hover colours are two steps apart on the surface
    // ladder, so the lift interpolates BETWEEN them rather than brightening
    // one - a brightened surface and surfaceRaised are not the same colour.
    g.setColour (selected
                     ? colour::surfaceHover
                     : colour::surface.interpolatedWith (
                           colour::surfaceRaised, hover.lift() / tokens::emphasis::surfaceLift));
    g.fillAll();

    const auto colourValue = entityColour::of (channel);

    g.setColour (colourValue);
    g.fillRect (0, 0, 4, getHeight());

    g.setColour (colour::divider);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    if (selected)
    {
        g.setColour (colour::accent);
        g.fillRect (0, 0, 4, getHeight());
        g.drawRect (getLocalBounds(), stroke::hairlinePx);
    }

    // The base pitch, so a melodic channel says what it is playing. Its
    // bounds come from resized() rather than being recomputed here, so it
    // cannot drift into the mute and solo buttons. An audio channel has the
    // arm toggle in this slot instead: a recording has no base pitch, and a
    // number that means nothing is worse than no number.
    if (ProjectEdits::playsNotes (channel))
    {
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::caption));
        g.drawText (juce::String ((int) channel[ids::basePitch]), pitchBounds,
                    juce::Justification::centredRight, false);
    }
}

void ChannelRackHeader::resized()
{
    auto area = getLocalBounds().reduced (space::sm, space::xs);
    area.removeFromLeft (space::xs);

    // Square, on the rung, and centred in the row rather than inset from it:
    // three sites spelled the width 22 by hand, and the vertical inset then
    // took four more off a target that has a floor.
    const auto letter = [] (juce::Rectangle<int> slot)
    { return slot.withSizeKeepingCentre (size::letterToggle, size::letterToggle); };

    soloButton.setBounds (letter (area.removeFromRight (size::letterToggle)));
    area.removeFromRight (space::xxs);
    muteButton.setBounds (letter (area.removeFromRight (size::letterToggle)));

    area.removeFromRight (space::sm);
    pitchBounds = area.removeFromRight (26);
    area.removeFromRight (space::xs);

    // The same slot the base pitch occupies, so the row's shape is the same
    // whichever kind of channel it is and the knobs never shift under the
    // cursor when a channel changes kind.
    armButton.setBounds (letter (pitchBounds.withWidth (size::letterToggle)));

    panKnob.setBounds (area.removeFromRight (size::knobSm));
    area.removeFromRight (space::xs);
    volumeKnob.setBounds (area.removeFromRight (size::knobSm));
    area.removeFromRight (space::xs);

    nameLabel.setBounds (area);
}

} // namespace dew
