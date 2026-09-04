#include "ui/MixerStrip.h"

#include <optional>
#include <utility>

#include "model/ProjectSchema.h"
#include "model/EntityColour.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/primitives/DewMeter.h"

namespace dew
{

MixerStrip::MixerStrip (ProjectDocument& d, juce::ValueTree t, bool isMasterStrip)
    : document (d)
    , track (std::move (t))
    , isMaster (isMasterStrip)
{
    nameLabel.setText (isMaster ? "Master" : track[ids::name].toString(),
                       juce::dontSendNotification);
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
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60,
                                tokens::size::controlHeightSm);
    const auto& gainSpec = requireMixerTrackParamSpec (ids::gain);
    gainSlider.setRange (gainSpec.minimum, gainSpec.maximum, gainSpec.interval);
    gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
    gainSlider.onDragStart = [this]
    {
        select();
        inDrag = true;
        gestureActive = false;
    };
    gainSlider.onDragEnd = [this]
    {
        inDrag = false;
        gestureActive = false;
    };
    gainSlider.onValueChange = [this]
    {
        ProjectEdits::setProperty (track, ids::gain, gainSlider.getValue(),
                                   &document.getUndoManager(), "Change level", gestureActive);

        gestureActive = inDrag;
    };
    addAndMakeVisible (gainSlider);

    if (! isMaster)
    {
        // Compact, like the channel rack's - it is a knob on a row, and its
        // caption lives in its tooltip. The two views name the same
        // parameter and now draw it the same size, in the same painter.
        panKnob.setCompact (true);
        panKnob.setTooltip ("Pan");
        panKnob.setValue ((double) track[ids::pan], juce::dontSendNotification);
        panKnob.onEditStart = [this]
        {
            select();
            inDrag = true;
            gestureActive = false;
        };
        panKnob.onEditEnd = [this]
        {
            inDrag = false;
            gestureActive = false;
        };
        panKnob.onValueChange = [this]
        {
            ProjectEdits::setProperty (track, ids::pan, panKnob.getValue(),
                                       &document.getUndoManager(), "Change pan", gestureActive);

            gestureActive = inDrag;
        };
        addAndMakeVisible (panKnob);

        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
        muteButton.onClick = [this]
        {
            select();
            ProjectEdits::setProperty (track, ids::mute, muteButton.getToggleState(),
                                       &document.getUndoManager(), "Mute");
        };
        addAndMakeVisible (muteButton);

        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
        soloButton.onClick = [this]
        {
            select();
            ProjectEdits::setProperty (track, ids::solo, soloButton.getToggleState(),
                                       &document.getUndoManager(), "Solo");
        };
        addAndMakeVisible (soloButton);
    }

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
        menu.addItem ((int) MenuItem::rename, "Rename");
        colourMenu::addTo (menu, track, colourBaseId);
    }

    const auto inserts = ProjectEdits::countMixerTracks (document.getState());

    menu.addItem ((int) MenuItem::addInsert, "Add insert", inserts < kMaxMixerTracks);

    // Rename / Add / - / Remove, which is the shape the rack's and the
    // playlist's menus already have: the separator sits immediately above
    // the destructive item and nothing else does.
    if (! isMaster)
    {
        menu.addSeparator();
        menu.addItem ((int) MenuItem::removeInsert, "Remove insert", inserts > 1);
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

    // The look and feel has to be set explicitly or DewLookAndFeel's popup
    // overrides do not apply, and a SafePointer because a menu outlives a
    // rebuild of the strips.
    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                            { event.getScreenX(), event.getScreenY(), 1, 1 }),
                        [safe = juce::Component::SafePointer<MixerStrip> (this)] (int choice)
                        {
                            if (safe != nullptr && choice > 0)
                                safe->applyMenuChoice (choice);
                        });
}

// --- painting and layout -----------------------------------------------------

void MixerStrip::paint (juce::Graphics& g)
{
    const auto body = getLocalBounds().toFloat().reduced (2.0f);

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

    if (effectCount > 0)
    {
        const auto badge = juce::Rectangle<float> ((float) getWidth() - 22.0f, 5.0f, 16.0f, 12.0f);

        g.setColour (tokens::colour::accent);
        g.fillRoundedRectangle (badge, tokens::radius::sm);

        g.setColour (tokens::colour::textOnAccent);
        g.setFont (tokens::type::font (tokens::type::caption, true));
        g.drawText (juce::String (effectCount), badge.toNearestInt(), juce::Justification::centred,
                    false);
    }
}

void MixerStrip::resized()
{
    using namespace tokens;

    auto area = getLocalBounds().reduced (space::sm, space::md);

    nameLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (space::xs);

    if (! isMaster)
    {
        // knobSm, the rung for a knob on a row - it was 38, a third size in
        // an application with two.
        panKnob.setBounds (
            area.removeFromTop (size::knobSm).withSizeKeepingCentre (size::knobSm, size::knobSm));
        area.removeFromTop (space::xs);

        // letterToggle rather than minTouchTarget, and inset sideways only: the
        // row was the floor itself and then gave two pixels back at the top and
        // bottom, which put the strip's M and S four pixels UNDER the floor.
        auto buttons = area.removeFromTop (size::letterToggle);
        muteButton.setBounds (
            buttons.removeFromLeft (buttons.getWidth() / 2).reduced (space::xxs, 0));
        soloButton.setBounds (buttons.reduced (space::xxs, 0));
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

    // These three carry their own hook now that they are Dew controls, and
    // it has to be the hook rather than a Trigger: the coverage gate asks
    // every knob in the window whether it has one.
    paramMenu::attachTo (host, panKnob, self, requireMixerTrackParamSpec (ids::pan));
    paramMenu::attachTo (host, muteButton, self, requireMixerTrackParamSpec (ids::mute));
    paramMenu::attachTo (host, soloButton, self, requireMixerTrackParamSpec (ids::solo));
}

void MixerStrip::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == ids::gain)
        gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
    else if (property == ids::pan)
        panKnob.setValue ((double) track[ids::pan], juce::dontSendNotification);
    else if (property == ids::mute)
        muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    else if (property == ids::solo)
        soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    else if (property == ids::name)
        nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
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

    g.setColour (level >= 1.0f                       ? colour::danger
                 : proportion > meter::hotProportion ? colour::warning
                                                     : colour::success);
    g.fillRoundedRectangle (bar, radius::xs);

    // Where the meter stops being green, said by POSITION as well as by hue -
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
        g.setFont (type::font (type::caption));
        g.drawText ("no channels", area, juce::Justification::centredTop, false);
        return;
    }

    g.setFont (type::font (type::caption));

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
