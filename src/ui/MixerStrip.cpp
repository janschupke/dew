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
                                   &document.getUndoManager(), "Rename mixer track");
    };
    addAndMakeVisible (nameLabel);

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
                                       &document.getUndoManager(), "Change level", continuing);
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
                                           &document.getUndoManager(), "Change pan", continuing);
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
                ProjectEdits::setPropertyOnEvery (track.getParent(), ids::MIXER_TRACK, ids::mute,
                                                  muted, &document.getUndoManager(),
                                                  muted ? "Turn every insert off"
                                                        : "Turn every insert on");
                return;
            }

            ProjectEdits::setProperty (track, ids::mute, muted, &document.getUndoManager(),
                                       muted ? "Turn insert off" : "Turn insert on");
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
    repaint();
}

void MixerStrip::select()
{
    if (onSelected != nullptr)
        onSelected();
}

// --- the pointer -------------------------------------------------------------

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! isMaster && nameLabel.getBounds().contains (event.getPosition()))
        nameLabel.showEditor();
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

    // A routing row names a channel; clicking it should go there.
    if (routingBounds.contains (event.getPosition()) && onChannelClicked != nullptr)
    {
        const auto row = (event.getPosition().y - routingBounds.getY() - tokens::space::xs)
                         / routingRowHeight;

        if (juce::isPositiveAndBelow (row, routedIds.size()))
            onChannelClicked (routedIds[row]);
    }
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
        case MenuItem::rename: nameLabel.showEditor(); return;

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

void MixerStrip::paint (juce::Graphics& g)
{
    const auto body = paint::bodyRect (*this, 2.0f);

    g.setColour (selected ? tokens::colour::surfaceRaised
                          : tokens::colour::surface.brighter (hover.lift()));
    g.fillRoundedRectangle (body, tokens::radius::md);

    // Master gets a neutral outline rather than an accent one: now that it
    // is selectable, an accent border on it always would read as selected.
    if (isMaster || selected)
    {
        g.setColour (selected ? tokens::colour::accent : tokens::colour::outline);
        g.drawRoundedRectangle (body, tokens::radius::md,
                                selected ? tokens::stroke::regular : tokens::stroke::hairline);
    }

    // A cap along the top edge, so which strip is selected is readable from
    // across the mixer rather than from a few percent of brightness.
    //
    // The same cap carries the strip's own colour when it has one and is not
    // selected. One band rather than two: selection is the louder fact and
    // has to win, and two stripes across a 60px strip is a pattern rather
    // than a signal.
    const auto cap = selected ? std::optional<juce::Colour> (tokens::colour::accent)
                              : entityColour::stored (track);

    if (cap.has_value())
    {
        g.setColour (*cap);
        g.fillRoundedRectangle (body.withHeight (3.0f), tokens::radius::xs);
    }

    paintMeter (g);
    paintRouting (g);

    // How many effects the strip carries, so it says what it holds without
    // having to be selected first. Top corner rather than the bottom, which
    // is where the fader's value box already is.
    const auto effectCount = ProjectEdits::countEffects (track);

    if (effectCount > 0 && ! badgeBounds.isEmpty())
    {
        g.setColour (tokens::colour::accent);
        g.fillRoundedRectangle (badgeBounds.toFloat(), tokens::radius::sm);

        g.setColour (tokens::colour::textOnAccent);
        g.setFont (tokens::type::font (tokens::type::caption, true));
        g.drawText (juce::String (effectCount), badgeBounds, juce::Justification::centred, false);
    }

    // Last, over the cap, the meter and the badge, the way a rack row and a
    // playlist header now do it.
    silence::paintOver (g, getLocalBounds(), (bool) track[ids::mute]);
}

void MixerStrip::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::sm, space::md);

    auto nameRow = area.removeFromTop (nameRowHeight);

    // The badge takes its slot from the NAME ROW, so the label is laid out in
    // what is left rather than underneath it. Only when there is something to
    // count: a strip with no effects gives the whole row to its name, which is
    // what every strip did before there was a badge at all.
    if (ProjectEdits::countEffects (track) > 0)
    {
        badgeBounds = nameRow.removeFromRight (size::glyphColumn)
                          .withSizeKeepingCentre (size::glyphColumn, size::captionBand);
        nameRow.removeFromRight (space::xxs);
    }
    else
        badgeBounds = {};

    nameLabel.setBounds (nameRow);
    area.removeFromTop (space::xs);

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

    // The routing list sits at the bottom; the fader and its meter take
    // what is left.
    routingBounds = isMaster
                        ? juce::Rectangle<int>()
                        : area.removeFromBottom (juce::jmin (routingHeight, area.getHeight() / 3));

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

void MixerStrip::paintMeter (juce::Graphics& g)
{
    using namespace tokens;

    if (meterBounds.isEmpty())
        return;

    const auto well = meterBounds.toFloat();

    g.setColour (colour::wellDeep);
    g.fillRoundedRectangle (well, radius::xs);

    if (level <= 0.0f)
        return;

    // Scaled the way a level is heard rather than by amplitude: linear, a
    // healthy mix sits in the bottom fifth of the meter and looks broken.
    const auto proportion = meter::proportionForGain (level);

    auto bar = well.withTop (well.getBottom() - proportion * well.getHeight());

    // funcLevel below the mark rather than success, and that is the whole of
    // what the function palette still says about a level. The controls that SET
    // one - this strip's fader, the rack's volume knob, an oscillator's gain -
    // took the app's own colour when level stopped being a function colour, and
    // a meter is the other half of that: not a control you hold but the signal
    // it passes, which is what funcLevel was named for. success stays what it
    // has always been, which is a verdict, and warning and danger stay the two
    // verdicts a meter is actually allowed to give.
    g.setColour (level >= 1.0f                       ? colour::danger
                 : proportion > meter::hotProportion ? colour::warning
                                                     : colour::funcLevel);
    g.fillRoundedRectangle (bar, radius::xs);

    // Where the meter stops being nominal, said by POSITION as well as by hue -
    // the bar's height carries the level, but the threshold it crosses was
    // carried by the colour change alone.
    const auto hotY = well.getBottom() - meter::hotProportion * well.getHeight();

    g.setColour (colour::dividerStrong);
    g.fillRect (well.getX(), hotY, well.getWidth(), stroke::hairline);
}

void MixerStrip::paintRouting (juce::Graphics& g)
{
    using namespace tokens;

    if (routingBounds.isEmpty())
        return;

    auto area = routingBounds;

    g.setColour (colour::divider);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    area.removeFromTop (space::xs);

    if (routedNames.isEmpty())
    {
        g.setColour (colour::textDisabled);
        g.setFont (type::font (type::small));
        g.drawText (tr (StringId::mixer_empty), area, juce::Justification::centredTop, false);
        return;
    }

    g.setFont (type::font (type::small));

    for (int i = 0; i < routedNames.size() && area.getHeight() >= routingRowHeight; ++i)
    {
        auto row = area.removeFromTop (routingRowHeight);

        const auto dot = row.removeFromLeft (8).withSizeKeepingCentre (5, 5).toFloat();
        g.setColour (i < routedColours.size() ? routedColours[i] : colour::textDisabled);
        g.fillEllipse (dot);

        g.setColour (colour::textSecondary);
        g.drawText (routedNames[i].toString(), row, juce::Justification::centredLeft, true);
    }
}

} // namespace dew
