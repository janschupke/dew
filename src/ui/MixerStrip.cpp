#include "ui/primitives/DewPaint.h"
#include "ui/MenuSeam.h"
#include "ui/MixerStrip.h"

#include <optional>
#include <utility>

#include "i18n/Strings.h"
#include "model/ProjectSchema.h"
#include "model/EntityColour.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/RowSilence.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/design/Gestures.h"
#include "ui/design/ParamPalette.h"
#include "ui/primitives/DewMeter.h"

namespace dew
{

MixerStrip::MixerStrip (ProjectDocument& d, juce::ValueTree t, bool isMasterStrip)
    : document (d)
    , track (std::move (t))
    , isMaster (isMasterStrip)
{
    nameLabel.setText (isMaster ? tr (StringId::automation_master) : track[ids::name].toString(),
                       juce::dontSendNotification);

    // One strip is one group. Beside the label rather than anywhere else so a
    // rename keeps the two in step - see the ids::name branch of valueTree-
    // PropertyChanged, which is the other place this has to be said.
    // The master answers to a name of its own. It is pinned outside the
    // viewport the inserts scroll in, so a walk of the holder no longer reaches
    // it and a test that wants it has to be able to ask.
    setComponentID (isMaster ? "mixerMaster" : "mixerStrip");
    setTitle (nameLabel.getText());
    setFocusContainerType (FocusContainerType::focusContainer);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (tokens::type::font (tokens::type::small, true));
    nameLabel.setEditable (false, ! isMaster, false);

    // The fader took the strip's whole remaining height and the label its
    // top, so selection was reachable only through a 6px border. The label
    // becomes inert and renaming moves to a double-click on the strip.
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.onTextChange = [this]
    {
        ProjectEdits::setProperty (track, ids::name, nameLabel.getText(),
                                   &document.getUndoManager(),
                                   TransactionName { "Rename mixer track" });
    };

    // INVISIBLE until somebody renames. The name on screen is painted, turned
    // on its side down the bottom of the strip, and a rotated juce::Label is
    // not something anybody can type into - so this is the editor and nothing
    // else, laid horizontally across the strip for as long as an edit lasts.
    nameLabel.onEditorHide = [this] { nameLabel.setVisible (false); };
    addChildComponent (nameLabel);

    // The strip sets clickable on itself, and JUCE asks the DEEPEST
    // component, so the fader has to say what it is or it inherits nothing
    // and shows an arrow in the middle of a strip that says "clickable".
    gainSlider.setMouseCursor (cursor::value);
    gainSlider.setSliderStyle (juce::Slider::LinearVertical);

    // A fader answers a DRAG, not a position.
    //
    // juce::Slider snaps to the pointer by default, so a press anywhere on the
    // track jumped the gain there and setMouseDragSensitivity - the one knob
    // that carries dew's shared drag distance - did not apply at all. That is
    // why "shift is finer, on every knob, fader and number field" was true of
    // the knob and the number field and not of this. Both halves are here now:
    // the distance at construction, and shift latched at press by fineDrag.
    gainSlider.setSliderSnapsToMousePosition (false);
    gainSlider.setMouseDragSensitivity (gesture::dragPixelsForFullRange);
    gainSlider.addMouseListener (&fineDrag, false);
    // A control's height, which is what every other input in dew is drawn at.
    // The fader gives up the six pixels; a readout that is typed into is an
    // input, and one 20 tall under a strip of 26s reads as a different kind of
    // thing rather than as the same thing lower down.
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, tokens::size::controlHeight);
    const auto& gainSpec = requireMixerTrackParamSpec (ids::gain);
    gainSlider.setRange (gainSpec.minimum, gainSpec.maximum, gainSpec.interval);

    // The one control in dew with no primitive of its own: a fader is drawn by
    // LookAndFeel_V4, which already reads these two ids. So the same colour
    // every other level control takes reaches it without a painter, and the
    // strip's fader and the channel rack's volume knob agree.
    const auto levelColour = palette::forRole (roleOf (ids::gain));
    gainSlider.setColour (juce::Slider::trackColourId, levelColour);
    gainSlider.setColour (juce::Slider::thumbColourId, levelColour);
    gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
    gesture.attach (
        gainSlider,
        [this] (bool continuing)
        {
            ProjectEdits::setProperty (track, ids::gain, gainSlider.getValue(),
                                       &document.getUndoManager(),
                                       TransactionName { "Change level" }, continuing);
            return true;
        },
        [this] { select(); });
    addAndMakeVisible (gainSlider);

    if (! isMaster)
    {
        // Compact, like the channel rack's - it is a knob on a row, and its
        // caption lives in its tooltip. The two views name the same
        // parameter and now draw it the same size, in the same painter.
        panKnob.setCompact (true);
        panKnob.setTooltip (tr (StringId::mixer_pan_name));
        panKnob.setValue ((double) track[ids::pan], juce::dontSendNotification);
        gesture.attach (
            panKnob,
            [this] (bool continuing)
            {
                ProjectEdits::setProperty (track, ids::pan, panKnob.getValue(),
                                           &document.getUndoManager(),
                                           TransactionName { "Change pan" }, continuing);
                return true;
            },
            [this] { select(); });
        addAndMakeVisible (panKnob);

        enabledButton.setComponentID ("stripEnabled");
        enabledButton.setClickingTogglesState (true);
        enabledButton.setOnColour (tokens::colour::warning);
        enabledButton.setTooltip (tr (StringId::mixer_enabled_help));
        enabledButton.onModifiedClick = [this] (const juce::ModifierKeys& mods)
        {
            select();

            const auto muted = enabledButton.getToggleState();

            // Every INSERT. Master is a different node type and so is left
            // alone by construction rather than by a special case.
            if (mods.isShiftDown())
            {
                ProjectEdits::setPropertyOnEvery (
                    track.getParent(), ids::MIXER_TRACK, ids::mute, muted,
                    &document.getUndoManager(),
                    TransactionName { muted ? "Turn every insert off" : "Turn every insert on" });
                return;
            }

            ProjectEdits::setProperty (
                track, ids::mute, muted, &document.getUndoManager(),
                TransactionName { muted ? "Turn insert off" : "Turn insert on" });
        };
        addAndMakeVisible (enabledButton);
    }

    applyMuteState();

    forwardChildMouseEventsTo (*this);
    setMouseCursor (cursor::clickable);

    track.addListener (this);
}

MixerStrip::~MixerStrip()
{
    track.removeListener (this);
}

void MixerStrip::setSelected (bool shouldBeSelected)
{
    if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
        repaint();
}

void MixerStrip::setLevel (float peak)
{
    level = meter::fall (level, peak, tickIntervalMs);

    if (! juce::approximatelyEqual (level, lastPaintedLevel))
    {
        lastPaintedLevel = level;
        repaint (meterBounds);
    }
}

void MixerStrip::setRouting (juce::Array<juce::var> names, juce::Array<juce::Colour> colours,
                             juce::Array<int> ids)
{
    routedNames = std::move (names);
    routedColours = std::move (colours);
    routedIds = std::move (ids);
    refreshRoutingTooltip();
    repaint();
}

void MixerStrip::select()
{
    if (onSelected != nullptr)
        onSelected();
}

// --- the pointer -------------------------------------------------------------

void MixerStrip::beginRename()
{
    if (isMaster)
        return;

    // Laid ACROSS the strip rather than down it - resized() put it there - for
    // the one reason the painted name is turned and this is not: a caret is not
    // something anybody can read sideways.
    nameLabel.setVisible (true);
    nameLabel.showEditor();
}

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (nameBounds.contains (event.getPosition()))
        beginRename();
}

void MixerStrip::mouseEnter (const juce::MouseEvent&)
{
    hover.enter();
}

void MixerStrip::mouseExit (const juce::MouseEvent&)
{
    hover.exit();
}

void MixerStrip::mouseDown (const juce::MouseEvent& event)
{
    // Selected first, so a menu always acts on the strip that was clicked
    // rather than on whatever was selected before it - the same rule
    // HeaderRow states for the rack and the playlist.
    select();

    // The menu is the STRIP's, so only a press on the strip may open it. A
    // press on the fader, the pan knob or a letter toggle arrives here too -
    // forwardChildMouseEventsTo - and every one of those carries a parameter
    // menu of its own, so both opened. See isOwnPress.
    if (! isOwnPress (event, *this))
        return;

    if (event.mods.isPopupMenu())
    {
        showMenu (event);
        return;
    }

    // The dots say how many channels arrive here; the list says which, and a
    // channel named in it is a channel you can go to. That last part is what
    // the painted rows were for, and it survives them.
    if (routingBounds.contains (event.getPosition()))
        showRoutingList (event.getPosition());
}

// --- the menu ----------------------------------------------------------------

juce::PopupMenu MixerStrip::buildMenu() const
{
    juce::PopupMenu menu;

    // Master has no name to change and no colour to be: it is the one strip
    // there is only ever one of, and nothing identifies it by colour. It
    // still offers "Add insert", because the mixer is what the menu is
    // about and the master is part of it.
    if (! isMaster)
    {
        addGlyphItem (menu, (int) MenuItem::rename, tr (StringId::mixer_menu_rename),
                      glyph::Action::rename);
        colourMenu::addTo (menu, track, colourBaseId);
    }

    const auto inserts = ProjectEdits::countMixerTracks (document.getState());

    addGlyphItem (menu, (int) MenuItem::addInsert, tr (StringId::mixer_menu_addInsert),
                  glyph::Action::add, inserts < kMaxMixerTracks);

    // Rename / Add / - / Remove, which is the shape the rack's and the
    // playlist's menus already have: the separator sits immediately above
    // the destructive item and nothing else does.
    if (! isMaster)
    {
        menu.addSeparator();
        addGlyphItem (menu, (int) MenuItem::removeInsert, tr (StringId::mixer_menu_removeInsert),
                      glyph::Action::remove, inserts > 1);
    }

    return menu;
}

void MixerStrip::applyMenuChoice (int choice)
{
    if (colourMenu::apply (choice, track, colourBaseId, document))
        return;

    switch ((MenuItem) choice)
    {
        case MenuItem::rename: beginRename(); return;

        case MenuItem::addInsert:
            if (onAddInsert)
                onAddInsert();

            return;

        case MenuItem::removeInsert:
        {
            // The id is read into a local FIRST, and no member is touched
            // afterwards. Removing an insert makes the document fire
            // valueTreeChildRemoved, which rebuilds the strips synchronously
            // and deletes `this` while this call is still on the stack.
            const auto id = getTrackId();
            const auto remove = onRemoveInsert;

            if (remove)
                remove (id);

            return;
        }
    }
}

void MixerStrip::showMenu (const juce::MouseEvent& event)
{
    auto menu = buildMenu();

    if (menu.getNumItems() == 0)
        return;

    showMenuAt<MixerStrip> (menu, *this, event,
                            [] (MixerStrip& strip, int choice) { strip.applyMenuChoice (choice); });
}

// --- painting and layout -----------------------------------------------------

void MixerStrip::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::sm, space::md);

    // The badge has a row of its own now that the name is not one. Only when
    // there is something to count: a strip with no effects gives the height
    // back to the fader, which is what every strip did before there was a
    // badge at all.
    if (ProjectEdits::countEffects (track) > 0)
    {
        badgeBounds = area.removeFromTop (badgeRowHeight)
                          .removeFromRight (size::glyphColumn)
                          .withSizeKeepingCentre (size::glyphColumn, size::captionBand);
        area.removeFromTop (space::xs);
    }
    else
        badgeBounds = {};

    if (! isMaster)
    {
        // knobSm, the rung for a knob on a row - it was 38, a third size in
        // an application with two.
        panKnob.setBounds (
            area.removeFromTop (size::knobSm).withSizeKeepingCentre (size::knobSm, size::knobSm));
        area.removeFromTop (space::xs);

        // One indicator where the M and the S were, centred in the column the
        // name is centred in - which is what "aligned with the name" means on a
        // strip, the axis here being vertical. letterToggle rather than
        // minTouchTarget, and no vertical inset: the row IS the floor, and
        // giving two pixels back at the top and bottom put the pair four pixels
        // under it.
        enabledButton.setBounds (
            area.removeFromTop (size::letterToggle)
                .withSizeKeepingCentre (size::letterToggle, size::letterToggle));
        area.removeFromTop (space::xs);
    }

    // The name goes at the BOTTOM, turned on its side - which is where a mixer
    // puts one, and the reason a strip no longer has to be as wide as the
    // longest name in the project. Master's name is as short as a name gets and
    // is laid out the same way, so the row of strips has one baseline.
    // At most a third of what is left, which is the rule the routing list used
    // to follow and for the same reason: a block that took a fixed height off a
    // short strip took it off the FADER, and a fader with no height left is a
    // strip with nothing to drag.
    nameBounds = area.removeFromBottom (juce::jmin (nameBlockHeight, area.getHeight() / 3));

    // The editor is laid out with everything else and only its VISIBILITY
    // moves. A control whose bounds appear the first time it is used is a
    // control with no size for as long as nobody has renamed anything, which is
    // most of the time - and is exactly what the strip's layout test looks for.
    nameLabel.setBounds (nameBounds.withHeight (size::controlHeightSm)
                             .withY (nameBounds.getCentreY() - size::controlHeightSm / 2));
    area.removeFromBottom (space::xs);

    // A row of coloured dots above it, one per channel arriving here. The names
    // are in this strip's tooltip and in the list a click on the row opens.
    routingBounds = isMaster ? juce::Rectangle<int>() : area.removeFromBottom (routingHeight);

    if (! isMaster)
        area.removeFromBottom (space::xs);

    meterBounds = area.removeFromRight (meterWidth).reduced (0, space::xxs);
    area.removeFromRight (space::xs);
    gainSlider.setBounds (area);
}

void MixerStrip::attachParamMenus (const paramMenu::Host* host)
{
    paramMenuTriggers.clear();

    if (host == nullptr || host->document == nullptr)
        return;

    const auto self = [this] { return track; };

    const auto watch = [this, host, &self] (juce::Component& control, const ParamSpec& spec)
    {
        paramMenuTriggers.push_back (
            std::make_unique<paramMenu::Trigger> (control, host->contextFor (self, spec)));
    };

    // The fader is still a juce::Slider - a vertical fader is not a knob and
    // there is no Dew primitive for one - so it still needs a Trigger
    // listening to it rather than a hook of its own.
    watch (gainSlider, requireMixerTrackParamSpec (ids::gain));

    // The master strip is a fader and nothing else - its pan and its
    // toggles are never built, so there is nothing there to watch.
    if (isMaster)
        return;

    // These two carry their own hook now that they are Dew controls, and it
    // has to be the hook rather than a Trigger: the coverage gate asks every
    // knob in the window whether it has one. There were three, and the third
    // named a solo that no longer exists.
    paramMenu::attachTo (host, panKnob, self, requireMixerTrackParamSpec (ids::pan));
    paramMenu::attachTo (host, enabledButton, self, requireMixerTrackParamSpec (ids::mute));
}

void MixerStrip::applyMuteState()
{
    const auto muted = (bool) track[ids::mute];

    enabledButton.setToggleState (muted, juce::dontSendNotification);

    // A muted insert dimmed NOTHING before this - it was told apart from a live
    // one by the colour of one 24px glyph. The master has no toggle and cannot
    // be muted, so nothing here reaches it.
    silence::applyTo (*this, muted, &enabledButton);
    repaint();
}

void MixerStrip::repaintForEffectChange (const juce::ValueTree& child)
{
    if (! child.hasType (ids::EFFECT))
        return;

    // resized(), not repaint(): the badge either appears or goes away, and the
    // name row's width depends on which. A repaint alone would draw the new
    // count into a slot laid out for the old one - or into no slot at all, the
    // first time an effect is added.
    resized();
    repaint();
}

void MixerStrip::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    repaintForEffectChange (child);
}

void MixerStrip::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    repaintForEffectChange (child);
}

void MixerStrip::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == ids::gain)
        gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
    else if (property == ids::pan)
        panKnob.setValue ((double) track[ids::pan], juce::dontSendNotification);
    else if (property == ids::mute)
        applyMuteState();
    else if (property == ids::name)
    {
        nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
        setTitle (nameLabel.getText());
    }
}

void MixerStrip::refreshRoutingTooltip()
{
    juce::StringArray names;

    for (const auto& name : routedNames)
        names.add (name.toString());

    // The strip's own tooltip, so hovering anywhere on it that is not a control
    // says what arrives here. TooltipWindow asks the DEEPEST component under
    // the pointer, and the fader, the pan knob and the toggle all answer for
    // themselves - which is right: what they do is not what this says.
    const auto joined = names.joinIntoString (", ");

    setTooltip (names.isEmpty()
                    ? tr (StringId::mixer_empty)
                    : tr (StringId::mixer_routing_help, Args {}.with ("channels", joined)));
}

void MixerStrip::showRoutingList (juce::Point<int> at)
{
    if (routedIds.isEmpty() || onChannelClicked == nullptr)
        return;

    juce::PopupMenu menu;

    for (int i = 0; i < routedIds.size(); ++i)
        menu.addItem (i + 1, routedNames[i].toString(), true, false);

    // Modal, like every other list in dew that you pick one thing out of, and
    // it keeps the affordance the painted rows had: a channel named here is a
    // channel you can go to.
    menu.showMenuAsync (
        juce::PopupMenu::Options {}
            .withParentComponent (getTopLevelComponent())
            .withTargetScreenArea (juce::Rectangle<int> (localPointToGlobal (at), { 1, 1 })),
        [this, ids = routedIds] (int chosen)
        {
            if (juce::isPositiveAndBelow (chosen - 1, ids.size()) && onChannelClicked != nullptr)
                onChannelClicked (ids[chosen - 1]);
        });
}

} // namespace dew
