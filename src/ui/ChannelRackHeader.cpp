#include "ui/ChannelRackHeader.h"

#include "i18n/Strings.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/RowSilence.h"
#include "ui/design/Cursors.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"

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

    // One channel is one group. refresh() says the same thing again, because a
    // rename reaches this label both ways.
    setTitle (nameLabel.getText());
    setFocusContainerType (FocusContainerType::focusContainer);

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

    enabledButton.setComponentID ("channelEnabled");
    enabledButton.setClickingTogglesState (true);
    enabledButton.setOnColour (tokens::colour::warning);
    enabledButton.setTooltip (tr (StringId::channelRack_enabled_help));
    enabledButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);

    // Selects on the CLICK. This was on onStateChange, which fires for every
    // internal transition - buttonNormal -> buttonOver among them - and the row
    // forwards its children's mouse events so it can light up on hover. Between
    // the two, merely moving the pointer across the indicator selected that
    // channel with no click at all.
    enabledButton.onModifiedClick = [this] (const juce::ModifierKeys& mods)
    {
        select();

        const auto muted = enabledButton.getToggleState();

        if (mods.isShiftDown())
        {
            ProjectEdits::setPropertyOnEvery (
                channel.getParent(), ids::CHANNEL, ids::muted, muted, &document.getUndoManager(),
                muted ? "Turn every channel off" : "Turn every channel on");
            return;
        }

        ProjectEdits::setProperty (channel, ids::muted, muted, &document.getUndoManager(),
                                   muted ? "Turn channel off" : "Turn channel on");
    };
    addAndMakeVisible (enabledButton);

    // Volume and pan on the row itself, so a pattern can be balanced without
    // selecting each channel in turn and reaching for the instrument panel.
    // Same ranges as the panel's VOLUME and PAN, so the two read the same
    // number, and no caption fits on a 34px row - pan is told from volume by
    // filling out from the centre.
    attachKnob (volumeKnob, ids::volume, "Change volume", "Volume");
    attachKnob (panKnob, ids::pan, "Change pan", "Pan");
    panKnob.setBipolar (true);

    // Base pitch and the mixer track, as numbers on the row. Both were in the
    // instrument panel and nowhere else, which meant routing a channel - or
    // reading what it was routed to - cost a selection each time.
    //
    // The range is the only thing they do not share. Base pitch has a ParamSpec
    // and takes the whole of MIDI from it; the mixer field's top is however
    // many tracks the mixer has, so refresh() sets it rather than the wiring.
    const auto& pitchSpec = requireInstrumentParamSpec (ids::basePitch);
    pitchField.setRange (pitchSpec.minimum, pitchSpec.maximum, pitchSpec.interval);
    pitchField.setNumDecimalPlaces (pitchSpec.decimals);
    attachField (pitchField, ids::basePitch, "Change base pitch",
                 tr (StringId::channelRack_pitch_help));

    mixerField.setNumDecimalPlaces (0);
    attachField (mixerField, ids::mixerTrackId, "Route channel",
                 tr (StringId::channelRack_mixer_help));

    // Only audio channels can be armed, and only one channel at a time -
    // clicking an armed row's R disarms it rather than arming a second.
    armButton.setTooltip (tr (StringId::channelRack_arm_help));
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
    setTitle (nameLabel.getText());
    enabledButton.setToggleState ((bool) channel[ids::muted], juce::dontSendNotification);
    volumeKnob.setValue ((double) channel[ids::volume], juce::dontSendNotification);
    panKnob.setValue ((double) channel[ids::pan], juce::dontSendNotification);

    // Taken again every refresh: a mixer track added or removed moves the top
    // of this range, and a field whose maximum is stale would clamp a perfectly
    // legal routing back down the next time it was dragged.
    mixerField.setRange (1.0, (double) mixerTrackCount(), 1.0);
    mixerField.setValue ((double) channel[ids::mixerTrackId], juce::dontSendNotification);

    // A recording has no base pitch, so the field is not shown for one. The arm
    // toggle takes the same slot - see pitchSlot.
    pitchField.setVisible (ProjectEdits::playsNotes (channel));
    pitchField.setValue ((double) channel[ids::basePitch], juce::dontSendNotification);

    const auto audio = ProjectEdits::playsClips (channel);
    armButton.setVisible (audio);
    armButton.setToggleState (audio && editorState.getArmedChannelId() == getChannelId(),
                              juce::dontSendNotification);

    // A muted channel's whole row recedes, controls included. It used to dim
    // only its STEPS, so the header of a channel that was not playing looked
    // exactly like the header of one that was.
    silence::applyTo (*this, (bool) channel[ids::muted], &enabledButton);

    resized();
    repaint();
}

// --- the menu ----------------------------------------------------------------

namespace
{

/** The id the instrument submenu gives one kind of channel. */
int addItemFor (InstrumentType type)
{
    switch (type)
    {
        case InstrumentType::synth: return (int) ChannelRackHeader::MenuItem::addSynth;
        case InstrumentType::audio: return (int) ChannelRackHeader::MenuItem::addAudio;
        case InstrumentType::soundfont: return (int) ChannelRackHeader::MenuItem::addSoundFont;
    }

    jassertfalse;
    return 0;
}

} // namespace

juce::PopupMenu ChannelRackHeader::buildMenu() const
{
    juce::PopupMenu menu;
    addGlyphItem (menu, (int) MenuItem::rename, tr (StringId::channelRack_menu_rename),
                  glyph::forAction (glyph::Action::rename));
    colourMenu::addTo (menu, channel, colourBaseId);

    // Built from the catalog rather than from three written-out rows, so a new
    // kind of instrument appears here because it exists. Nothing in the UI read
    // instrumentDescriptors before this; the three buttons under the list each
    // knew their own kind and none of them knew there were three.
    juce::PopupMenu kinds;

    for (const auto& instrument : instrumentDescriptors())
        addGlyphItem (kinds, addItemFor (instrument.type), tr (instrument.displayName),
                      glyph::forInstrument (instrument.type));

    addGlyphSubMenu (menu, tr (StringId::channelRack_menu_addChannel), std::move (kinds),
                     glyph::forAction (glyph::Action::add));

    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::removeChannel,
                  tr (StringId::channelRack_menu_removeChannel),
                  glyph::forAction (glyph::Action::remove));
    return menu;
}

void ChannelRackHeader::applyMenuChoice (int choice)
{
    if (colourMenu::apply (choice, channel, colourBaseId, document))
        return;

    switch ((MenuItem) choice)
    {
        case MenuItem::rename: nameLabel.showEditor(); break;

        // The parent row of the submenu. It carries no id, so choosing it is
        // not something that can happen; the case is here so the switch still
        // names every item and the compiler still checks that it does.
        case MenuItem::addChannel: break;

        case MenuItem::addSynth:
        case MenuItem::addAudio:
        case MenuItem::addSoundFont:
            if (onAddChannel)
                for (const auto& instrument : instrumentDescriptors())
                    if (addItemFor (instrument.type) == choice)
                        onAddChannel (instrument.type);
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

void ChannelRackHeader::attachField (DewNumberField& field, const juce::Identifier& property,
                                     const juce::String& transactionName,
                                     const juce::String& tooltip)
{
    field.setTooltip (tooltip);

    field.onEditStart = [this]
    {
        select();
        gestureActive = false;
    };

    field.onValueChange = [this, &field, property, transactionName]
    {
        if (updating)
            return;

        // Integers, because both of these are: a MIDI note number and a track
        // id. Writing the double a field carries would put 60.0 in the file
        // where every other writer of these two puts 60.
        ProjectEdits::setProperty (channel, property, juce::roundToInt (field.getValue()),
                                   &document.getUndoManager(), transactionName, gestureActive);

        gestureActive = true;
    };

    addAndMakeVisible (field);
}

int ChannelRackHeader::mixerTrackCount() const
{
    auto count = 0;

    for (const auto& track : document.getState().getChildWithName (ids::MIXER))
        if (track.hasType (ids::MIXER_TRACK))
            ++count;

    // Never zero: a range whose top is below its bottom is not a range, and a
    // project is never without a master track anyway.
    return juce::jmax (1, count);
}

void ChannelRackHeader::attachParamMenus (const paramMenu::Host* host)
{
    const auto self = [this] { return channel; };

    paramMenu::attachTo (host, volumeKnob, self, requireInstrumentParamSpec (ids::volume));
    paramMenu::attachTo (host, panKnob, self, requireInstrumentParamSpec (ids::pan));
    paramMenu::attachTo (host, enabledButton, self, requireInstrumentParamSpec (ids::muted));
    paramMenu::attachTo (host, pitchField, self, requireInstrumentParamSpec (ids::basePitch));

    // mixerField deliberately gets none. Which track a channel plays through is
    // a RELATION between two objects rather than a quantity - there is nothing
    // for a curve over it to mean, and it has no ParamSpec to reset it to. It
    // is the second of the two controls ParamMenuTests names as exceptions.
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

    silence::paintOver (g, getLocalBounds(), (bool) channel[ids::muted]);
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

    enabledButton.setBounds (letter (area.removeFromRight (size::letterToggle)));

    area.removeFromRight (space::sm);
    mixerField.setBounds (area.removeFromRight (size::rowField));
    area.removeFromRight (space::xs);

    pitchSlot = area.removeFromRight (size::rowField);
    pitchField.setBounds (pitchSlot);
    area.removeFromRight (space::xs);

    // The same slot the base pitch occupies, so the row's shape is the same
    // whichever kind of channel it is and the knobs never shift under the
    // cursor when a channel changes kind.
    armButton.setBounds (letter (pitchSlot.withWidth (size::letterToggle)));

    panKnob.setBounds (area.removeFromRight (size::knobSm));
    area.removeFromRight (space::xs);
    volumeKnob.setBounds (area.removeFromRight (size::knobSm));
    area.removeFromRight (space::xs);

    nameLabel.setBounds (area);
}

} // namespace dew
