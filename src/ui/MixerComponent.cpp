#include "ui/MixerComponent.h"

#include <vector>

#include <memory>

#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "ui/MenuSeam.h"
#include "ui/primitives/DewMeter.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/HoverTracker.h"

#include <optional>

#include "model/EntityColour.h"
#include "ui/ColourMenu.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "ui/design/DewLookAndFeel.h"

namespace dew
{

using namespace tokens;

/** One mixer strip. Master has no pan, mute or solo, so it is the same class
    with those controls hidden rather than a second nearly-identical one.
*/
class MixerComponent::Strip : public juce::Component, private juce::ValueTree::Listener
{
public:
    Strip (ProjectDocument& d, juce::ValueTree t, bool isMasterStrip)
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

    ~Strip() override
    {
        track.removeListener (this);
    }

    void setSelected (bool shouldBeSelected)
    {
        if (std::exchange (selected, shouldBeSelected) != shouldBeSelected)
            repaint();
    }

    /** New peak from the engine, 0..1. Falls back smoothly rather than snapping,
        so a meter reads as a level and not as a flicker.
    */
    void setLevel (float peak)
    {
        level = meter::fall (level, peak, tickIntervalMs);

        if (! juce::approximatelyEqual (level, lastPaintedLevel))
        {
            lastPaintedLevel = level;
            repaint (meterBounds);
        }
    }

    /** Names of the channels routed into this strip, with their colours and
        ids, so a routing row is a way to reach the channel it names.
    */
    void setRouting (juce::Array<juce::var> names, juce::Array<juce::Colour> colours,
                     juce::Array<int> ids)
    {
        routedNames = std::move (names);
        routedColours = std::move (colours);
        routedIds = std::move (ids);
        repaint();
    }

    std::function<void (int channelId)> onChannelClicked;

    bool isMasterStrip() const noexcept
    {
        return isMaster;
    }
    int getTrackId() const
    {
        return (int) track[ids::id];
    }

    std::function<void()> onSelected;

    /** Adding and removing are the MIXER's edits, not a strip's: the strip is
        the surface the menu opened on and is deleted by the rebuild either one
        causes, so it reports the gesture and touches nothing afterwards. */
    std::function<void()> onAddInsert;
    std::function<void (int trackId)> onRemoveInsert;

    void select()
    {
        if (onSelected != nullptr)
            onSelected();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! isMaster && nameLabel.getBounds().contains (event.getPosition()))
            nameLabel.showEditor();
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        hover.enter();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover.exit();
    }

    void mouseDown (const juce::MouseEvent& event) override
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

    // --- the menu ------------------------------------------------------------
    // Built and applied by named methods rather than by a lambda inside
    // showMenuAsync, for the reason MenuSeam.h gives: showMenuAsync cannot be
    // driven headlessly, so a test reads the built menu instead. A strip is not
    // a HeaderRow - it holds a fader rather than a name and two toggles - so it
    // states the three lines rather than inheriting them.
    enum class MenuItem
    {
        rename = 1,
        addInsert,
        removeInsert
    };

    static constexpr int colourBaseId = (int) MenuItem::removeInsert + 1;

    juce::PopupMenu buildMenu() const
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

    void applyMenuChoice (int choice)
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

private:
    void showMenu (const juce::MouseEvent& event)
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
                            [safe = juce::Component::SafePointer<Strip> (this)] (int choice)
                            {
                                if (safe != nullptr && choice > 0)
                                    safe->applyMenuChoice (choice);
                            });
    }

public:
    void paint (juce::Graphics& g) override
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
            const auto badge = juce::Rectangle<float> ((float) getWidth() - 22.0f, 5.0f, 16.0f,
                                                       12.0f);

            g.setColour (tokens::colour::accent);
            g.fillRoundedRectangle (badge, tokens::radius::sm);

            g.setColour (tokens::colour::textOnAccent);
            g.setFont (tokens::type::font (tokens::type::caption, true));
            g.drawText (juce::String (effectCount), badge.toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        using namespace tokens;

        auto area = getLocalBounds().reduced (space::sm, space::md);

        nameLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (space::xs);

        if (! isMaster)
        {
            // knobSm, the rung for a knob on a row - it was 38, a third size in
            // an application with two.
            panKnob.setBounds (area.removeFromTop (size::knobSm)
                                   .withSizeKeepingCentre (size::knobSm, size::knobSm));
            area.removeFromTop (space::xs);

            auto buttons = area.removeFromTop (size::minTouchTarget);
            muteButton.setBounds (
                buttons.removeFromLeft (buttons.getWidth() / 2).reduced (space::xxs));
            soloButton.setBounds (buttons.reduced (space::xxs));
            area.removeFromTop (space::xs);
        }

        // The routing list sits at the bottom; the fader and its meter take
        // what is left.
        routingBounds = isMaster ? juce::Rectangle<int>()
                                 : area.removeFromBottom (
                                       juce::jmin (routingHeight, area.getHeight() / 3));

        meterBounds = area.removeFromRight (meterWidth).reduced (0, space::xxs);
        area.removeFromRight (space::xs);
        gainSlider.setBounds (area);
    }

    /** The controls on this strip that were built from a ParamSpec.

        The master strip has a fader and nothing else, so its pan and its
        toggles are not built at all - attachTo on a control that does not exist
        would be a crash, which is why each of these is guarded by the same flag
        that built it.
    */
    void attachParamMenus (const paramMenu::Host* host)
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

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
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

    ProjectDocument& document;
    juce::ValueTree track;
    void paintMeter (juce::Graphics& g)
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
    }

    void paintRouting (juce::Graphics& g)
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

    static constexpr int meterWidth = 8;
    static constexpr int routingHeight = 58;

    /** One row of the routing list. The painter and the hit test both need it
        and both had it written out, which is a click that selects the wrong
        channel the moment one of them changes. */
    static constexpr int routingRowHeight = 12;
    static constexpr int tickIntervalMs = 1000 / tokens::motion::uiRefreshHz;

    bool isMaster;
    bool selected = false;
    HoverTracker hover { *this };

    float level = 0.0f;
    float lastPaintedLevel = -1.0f;

    juce::Rectangle<int> meterBounds, routingBounds;
    juce::Array<juce::var> routedNames;
    juce::Array<juce::Colour> routedColours;
    juce::Array<int> routedIds;

    /** True between a fader's onDragStart and onDragEnd. One flag for the strip:
        only one control can be under the pointer at a time.
    */
    bool inDrag = false;
    bool gestureActive = false;

    juce::Label nameLabel;
    juce::Slider gainSlider;

    /** The same two controls the channel rack's rows carry, at the same size and
        in the same painter.

        These were a bare rotary juce::Slider at 38px and two juce::TextButtons,
        which is a third knob size and a second idea of what a mute letter looks
        like - in the one view whose whole job is to sit beside the others and
        agree with them. They also had no right-click menu of their own, which
        the comment beside attachParamMenus used to state as a cost worth
        paying; carrying it is what pays it back.
    */
    DewKnob panKnob { requireMixerTrackParamSpec (ids::pan) };
    DewLetterToggle muteButton { "M", tokens::colour::warning, "Mute this track" };
    DewLetterToggle soloButton { "S", tokens::colour::accent, "Solo this track" };

    /** Owned here, and destroyed before the controls they watch. */
    std::vector<std::unique_ptr<paramMenu::Trigger>> paramMenuTriggers;
};

// -----------------------------------------------------------------------------

void MixerComponent::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The strips are built before the host arrives, so this re-attaches rather
    // than only recording the pointer for the next rebuild.
    for (auto* strip : strips)
        strip->attachParamMenus (host);
}

MixerComponent::MixerComponent (ProjectDocument& d, EditorState& s, AudioEngine* e)
    : document (d)
    , editorState (s)
    , engine (e)
    , chainHost (d, s, EffectChainHost::Orientation::horizontal)
{
    setComponentID ("mixer");
    confirmDestructive = confirmWithPanel (this);

    // Strips scroll. removeFromLeft on a fixed rectangle clamps at the right
    // edge, so once the strips outran the window every further one - including
    // the master, which is added last - was silently given no width at all.
    // That mattered at four inserts and matters more now that there can be
    // thirty-two of them.
    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    addStripButton.setComponentID ("addMixerTrack");
    addStripButton.setTooltip ("Add a mixer insert");
    addStripButton.onClick = [this] { addMixerTrack(); };
    stripHolder.addAndMakeVisible (addStripButton);

    if (engine != nullptr)
        startTimerHz (tokens::motion::uiRefreshHz);

    addAndMakeVisible (chainHost);

    editorState.addChangeListener (this);

    document.getState().addListener (this);
    rebuildStrips();
}

MixerComponent::~MixerComponent()
{
    editorState.removeChangeListener (this);
    document.getState().removeListener (this);
}

void MixerComponent::refresh()
{
    document.getState().addListener (this);
    rebuildStrips();
}

void MixerComponent::rebuildStrips()
{
    strips.clear();

    const auto mixer = document.getState().getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
        {
            auto* strip = strips.add (new Strip (document, track, false));
            strip->attachParamMenus (paramMenuHost);
            const auto id = (int) track[ids::id];
            strip->onSelected = [this, id] { editorState.setSelectedMixerTrackId (id); };
        }

    // Master had no onSelected at all, so clicking it did nothing and its chain
    // could never be edited. It selects like any other strip now.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        auto* strip = strips.add (new Strip (document, master, true));
        strip->attachParamMenus (paramMenuHost);
        strip->onSelected = [this] { editorState.setSelectedMixerTrackId (masterTrackId); };
    }

    // Both edits belong to the mixer, not to the strip the menu opened on: that
    // strip is deleted by the rebuild either one causes.
    for (auto* strip : strips)
    {
        strip->onAddInsert = [this] { addMixerTrack(); };
        strip->onRemoveInsert = [this] (int id) { removeMixerTrack (id); };
    }

    for (auto* strip : strips)
        stripHolder.addAndMakeVisible (strip);

    updateRouting();
    pointChainAtSelectedTrack();
    resized();
    repaint();
}

void MixerComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::MIXER_TRACK))
        rebuildStrips();
}

void MixerComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::MIXER_TRACK))
        rebuildStrips();
}

void MixerComponent::paint (juce::Graphics& g)
{
    g.fillAll (tokens::colour::background);
}

void MixerComponent::pointChainAtSelectedTrack()
{
    const auto selectedId = editorState.getSelectedMixerTrackId();
    const auto mixer = document.getState().getChildWithName (ids::MIXER);

    // The master is a bus like any other and carries its own chain, so it
    // resolves here rather than being excluded.
    auto selectedTrack = selectedId == masterTrackId ? mixer.getChildWithName (ids::MASTER)
                                                     : juce::ValueTree();

    if (selectedId != masterTrackId)
        for (const auto& track : mixer)
            if (track.hasType (ids::MIXER_TRACK) && (int) track[ids::id] == selectedId)
                selectedTrack = track;

    chainHost.setOwner (selectedTrack, ! selectedTrack.isValid() ? juce::String()
                                       : selectedId == masterTrackId
                                           ? "Master"
                                           : selectedTrack[ids::name].toString());

    for (auto* strip : strips)
        strip->setSelected (strip->isMasterStrip() ? selectedId == masterTrackId
                                                   : strip->getTrackId() == selectedId);
}

void MixerComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pointChainAtSelectedTrack();
}

void MixerComponent::timerCallback()
{
    if (engine == nullptr)
        return;

    for (int i = 0; i < strips.size(); ++i)
    {
        auto* strip = strips[i];
        strip->setLevel (strip->isMasterStrip() ? engine->readAndClearMasterPeak()
                                                : engine->readAndClearTrackPeak (i));
    }
}

void MixerComponent::updateRouting()
{
    // Which channels feed each insert. Without this the mixer is a row of
    // anonymous faders and there is nothing to say what any of them carries.
    for (auto* strip : strips)
    {
        juce::Array<juce::var> names;
        juce::Array<juce::Colour> colours;
        juce::Array<int> channelIds;

        if (! strip->isMasterStrip())
        {
            for (const auto& channel : document.getState())
            {
                if (! channel.hasType (ids::CHANNEL)
                    || (int) channel[ids::mixerTrackId] != strip->getTrackId())
                    continue;

                names.add (channel[ids::name].toString());
                colours.add (entityColour::of (channel));
                channelIds.add ((int) channel[ids::id]);
            }
        }

        strip->onChannelClicked = [this] (int channelId)
        {
            editorState.setSelectedChannelId (channelId);

            if (onShowChannelRack != nullptr)
                onShowChannelRack();
        };

        strip->setRouting (std::move (names), std::move (colours), std::move (channelIds));
    }
}

void MixerComponent::resized()
{
    auto area = getLocalBounds().reduced (space::md);

    // The chain row gets the bottom of the panel, at exactly the height one row
    // of cards needs: strips need the rest and a fader is useless once it is
    // shorter than a thumb. Still halved as a floor, for a very short window.
    const auto wanted = chainHost.getPreferredHeight() + tokens::space::md;
    auto chainArea = area.removeFromBottom (juce::jmin (wanted, area.getHeight() / 2));

    // Full width. The cards size themselves and scroll; it is the row that has
    // the width to give them.
    chainHost.setBounds (chainArea.withTrimmedTop (tokens::space::md));

    stripViewport.setBounds (area);

    // One column past the last strip, for the add button. The rack puts its add
    // button in the next empty ROW of the list rather than in a footer strip;
    // the mixer is read across, so its analogue is the next empty column.
    const auto columns = strips.size() + 1;
    const auto contentWidth = juce::jmax (stripViewport.getMaximumVisibleWidth(),
                                          columns * size::mixerStripWidth);
    stripHolder.setSize (contentWidth, stripViewport.getMaximumVisibleHeight());

    auto holder = stripHolder.getLocalBounds();

    for (auto* strip : strips)
        strip->setBounds (holder.removeFromLeft (size::mixerStripWidth));

    // Top of the column, level with the strip names rather than centred down a
    // 500px lane, where it read as something dropped rather than offered.
    addStripButton.setBounds (holder.removeFromLeft (size::mixerStripWidth)
                                  .withHeight (size::iconButton)
                                  .translated (0, space::md)
                                  .withSizeKeepingCentre (size::iconButton, size::iconButton));
}

void MixerComponent::addMixerTrack()
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add insert");

    const auto added = ProjectEdits::addMixerTrack (document.getState(), {}, &undo);

    if (added.isValid())
        editorState.setSelectedMixerTrackId ((int) added[ids::id]);
}

void MixerComponent::removeMixerTrack (int mixerTrackId)
{
    const auto track = ProjectEdits::findMixerTrack (document.getState(), mixerTrackId);

    if (! track.isValid())
        return;

    ConfirmPanel::Request request;
    request.title = "Remove insert";
    request.message = "Remove \"" + track[ids::name].toString()
                      + "\"? Its effects go with it, and anything routed into it moves to the "
                        "first insert.";
    request.confirmText = "Remove";

    confirmDestructive (
        request,
        [this, mixerTrackId]
        {
            auto found = ProjectEdits::findMixerTrack (document.getState(), mixerTrackId);

            if (! found.isValid())
                return;

            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Remove insert");

            if (! ProjectEdits::removeMixerTrack (document.getState(), found, &undo))
                return;

            // Session state, so it is set HERE and not inside the edit - it must
            // not go on the undo stack. After the edit, because the rebuild has
            // already run by then and pointed the chain host at nothing.
            if (editorState.getSelectedMixerTrackId() == mixerTrackId)
                editorState.setSelectedMixerTrackId (masterTrackId);
        });
}

MixerComponent::Strip* MixerComponent::stripFor (int mixerTrackId) const
{
    for (auto* strip : strips)
        if (strip->getTrackId() == mixerTrackId)
            return strip;

    return nullptr;
}

bool MixerComponent::applyMixerTrackMenuChoice (int mixerTrackId, int choice)
{
    auto* strip = stripFor (mixerTrackId);

    if (strip == nullptr)
        return false;

    strip->applyMenuChoice (choice);
    return true;
}

juce::StringArray MixerComponent::mixerTrackMenuItems (int mixerTrackId) const
{
    auto* strip = stripFor (mixerTrackId);

    if (strip == nullptr)
        return {};

    // Bound to a named local: PopupMenu::MenuItemIterator keeps a REFERENCE, so
    // iterating one returned by value walks a destroyed object.
    const auto menu = strip->buildMenu();
    return menuItems (menu);
}

} // namespace dew
