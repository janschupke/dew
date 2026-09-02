#include "MixerComponent.h"

#include "../model/ProjectEdits.h"
#include "design/Tokens.h"

#include "../model/Ids.h"
#include "DewLookAndFeel.h"

namespace dew
{

/** One mixer strip. Master has no pan, mute or solo, so it is the same class
    with those controls hidden rather than a second nearly-identical one.
*/
class MixerComponent::Strip : public juce::Component,
                              private juce::ValueTree::Listener
{
public:
    Strip (ProjectDocument& d, juce::ValueTree t, bool isMasterStrip)
        : document (d), track (std::move (t)), isMaster (isMasterStrip)
    {
        nameLabel.setText (isMaster ? "Master" : track[ids::name].toString(),
                           juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        nameLabel.setEditable (false, ! isMaster, false);

        // The fader took the strip's whole remaining height and the label its
        // top, so selection was reachable only through a 6px border. The label
        // becomes inert and renaming moves to a double-click on the strip.
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.onTextChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Rename mixer track");
            track.setProperty (ids::name, nameLabel.getText(), &undo);
        };
        addAndMakeVisible (nameLabel);

        gainSlider.setSliderStyle (juce::Slider::LinearVertical);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        gainSlider.setRange (0.0, 1.5, 0.001);
        gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
        gainSlider.onDragStart = [this]
        {
            select();
            document.getUndoManager().beginNewTransaction ("Change level");
        };
        gainSlider.onValueChange = [this]
        {
            auto& undo = document.getUndoManager();
            undo.beginNewTransaction ("Change level");
            track.setProperty (ids::gain, gainSlider.getValue(), &undo);
        };
        addAndMakeVisible (gainSlider);

        if (! isMaster)
        {
            panSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            panSlider.setRange (-1.0, 1.0, 0.001);
            panSlider.setValue ((double) track[ids::pan], juce::dontSendNotification);
            panSlider.onDragStart = [this]
            {
                select();
                document.getUndoManager().beginNewTransaction ("Change pan");
            };
            panSlider.onValueChange = [this]
            {
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Change pan");
                track.setProperty (ids::pan, panSlider.getValue(), &undo);
            };
            addAndMakeVisible (panSlider);

            muteButton.setButtonText ("M");
            muteButton.setClickingTogglesState (true);
            muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
            muteButton.onClick = [this]
            {
                select();
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Mute");
                track.setProperty (ids::mute, muteButton.getToggleState(), &undo);
            };
            addAndMakeVisible (muteButton);

            soloButton.setButtonText ("S");
            soloButton.setClickingTogglesState (true);
            soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
            soloButton.onClick = [this]
            {
                select();
                auto& undo = document.getUndoManager();
                undo.beginNewTransaction ("Solo");
                track.setProperty (ids::solo, soloButton.getToggleState(), &undo);
            };
            addAndMakeVisible (soloButton);
        }

        forwardChildMouseEventsTo (*this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

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
        // Rise instantly, fall over about a third of a second: a meter that
        // fell as fast as it rose would be unreadable on percussive material.
        level = peak > level ? peak : level * 0.82f + peak * 0.18f;

        if (level < 0.001f)
            level = 0.0f;

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

    bool isMasterStrip() const noexcept { return isMaster; }
    int getTrackId() const { return (int) track[ids::id]; }

    std::function<void()> onSelected;

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

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (isMouseOver (true)); }

    void mouseDown (const juce::MouseEvent& event) override
    {
        select();

        // A routing row names a channel; clicking it should go there.
        if (routingBounds.contains (event.getPosition()) && onChannelClicked != nullptr)
        {
            const auto row = (event.getPosition().y - routingBounds.getY() - tokens::space::xs) / 12;

            if (juce::isPositiveAndBelow (row, routedIds.size()))
                onChannelClicked (routedIds[row]);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto body = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (selected ? tokens::colour::surfaceRaised
                              : hovered ? tokens::colour::surface.brighter (0.06f)
                                        : tokens::colour::surface);
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
        if (selected)
        {
            g.setColour (tokens::colour::accent);
            g.fillRoundedRectangle (body.withHeight (3.0f), 1.5f);
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
            g.fillRoundedRectangle (badge, 3.0f);

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
            panSlider.setBounds (area.removeFromTop (38).reduced (space::lg, 0));
            area.removeFromTop (space::xs);

            auto buttons = area.removeFromTop (size::minTouchTarget);
            muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1));
            soloButton.setBounds (buttons.reduced (1));
            area.removeFromTop (space::xs);
        }

        // The routing list sits at the bottom; the fader and its meter take
        // what is left.
        routingBounds = isMaster ? juce::Rectangle<int>()
                                 : area.removeFromBottom (juce::jmin (routingHeight, area.getHeight() / 3));

        meterBounds = area.removeFromRight (meterWidth).reduced (0, space::xxs);
        area.removeFromRight (space::xs);
        gainSlider.setBounds (area);
    }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (property == ids::gain)
            gainSlider.setValue ((double) track[ids::gain], juce::dontSendNotification);
        else if (property == ids::pan)
            panSlider.setValue ((double) track[ids::pan], juce::dontSendNotification);
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
        g.fillRoundedRectangle (well, 2.0f);

        if (level <= 0.0f)
            return;

        // Scaled the way a level is heard rather than by amplitude: linear, a
        // healthy mix sits in the bottom fifth of the meter and looks broken.
        const auto db = juce::Decibels::gainToDecibels (level, (float) meterFloorDb);
        const auto proportion = juce::jlimit (0.0f, 1.0f,
                                              (float) ((db - meterFloorDb) / -meterFloorDb));

        auto bar = well.withTop (well.getBottom() - proportion * well.getHeight());

        g.setColour (level >= 1.0f ? colour::danger
                                   : level > 0.7f ? colour::warning : colour::success);
        g.fillRoundedRectangle (bar, 2.0f);
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

        for (int i = 0; i < routedNames.size() && area.getHeight() >= 12; ++i)
        {
            auto row = area.removeFromTop (12);

            const auto dot = row.removeFromLeft (8).withSizeKeepingCentre (5, 5).toFloat();
            g.setColour (i < routedColours.size() ? routedColours[i] : colour::textDisabled);
            g.fillEllipse (dot);

            g.setColour (colour::textSecondary);
            g.drawText (routedNames[i].toString(), row, juce::Justification::centredLeft, true);
        }
    }

    void setHovered (bool shouldBeHovered)
    {
        if (std::exchange (hovered, shouldBeHovered) != shouldBeHovered)
            repaint();
    }

    static constexpr int meterWidth = 8;
    static constexpr int routingHeight = 58;
    static constexpr double meterFloorDb = -48.0;

    bool isMaster;
    bool selected = false;
    bool hovered = false;

    float level = 0.0f;
    float lastPaintedLevel = -1.0f;

    juce::Rectangle<int> meterBounds, routingBounds;
    juce::Array<juce::var> routedNames;
    juce::Array<juce::Colour> routedColours;
    juce::Array<int> routedIds;

    juce::Label nameLabel;
    juce::Slider gainSlider;
    juce::Slider panSlider;
    juce::TextButton muteButton, soloButton;
};

// -----------------------------------------------------------------------------

MixerComponent::MixerComponent (ProjectDocument& d, EditorState& s, AudioEngine* e)
    : document (d), editorState (s), engine (e), effectChain (d, s)
{
    setComponentID ("mixer");

    // Strips scroll. removeFromLeft on a fixed rectangle clamps at the right
    // edge, so past about twelve inserts every further strip - including the
    // master, which is added last - was silently given no width at all.
    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    if (engine != nullptr)
        startTimerHz (tokens::motion::uiRefreshHz);

    chainViewport.setViewedComponent (&effectChain, false);
    chainViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (chainViewport);

    effectChain.onRequiredHeightChanged = [this] { layOutChain(); };
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
            const auto id = (int) track[ids::id];
            strip->onSelected = [this, id] { editorState.setSelectedMixerTrackId (id); };
        }

    // Master had no onSelected at all, so clicking it did nothing and its chain
    // could never be edited. It selects like any other strip now.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        auto* strip = strips.add (new Strip (document, master, true));
        strip->onSelected = [this] { editorState.setSelectedMixerTrackId (masterTrackId); };
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
    g.fillAll (Palette::background);
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

    effectChain.setOwner (selectedTrack);
    effectChain.setOwnerName (! selectedTrack.isValid() ? juce::String()
                              : selectedId == masterTrackId ? "Master"
                                                            : selectedTrack[ids::name].toString());

    for (auto* strip : strips)
        strip->setSelected (strip->isMasterStrip() ? selectedId == masterTrackId
                                                   : strip->getTrackId() == selectedId);
}

void MixerComponent::layOutChain()
{
    const auto width = juce::jmax (120, chainViewport.getMaximumVisibleWidth());
    effectChain.setSize (width, juce::jmax (chainViewport.getHeight(),
                                            effectChain.getRequiredHeight()));
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
                colours.add (juce::Colour::fromString (
                    "ff" + channel[ids::colour].toString().getLastCharacters (6)));
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
    auto area = getLocalBounds().reduced (8);

    // The chain editor gets the bottom of the panel, at a fixed height: strips
    // need the rest and a fader is useless once it is shorter than a thumb.
    auto chainArea = area.removeFromBottom (juce::jmin (chainHeight, area.getHeight() / 2));

    // Capped rather than stretched: a number field wider than a hand is not
    // easier to drag, only emptier.
    chainViewport.setBounds (chainArea.withTrimmedTop (tokens::space::md)
                                      .withWidth (juce::jmin (chainWidth, chainArea.getWidth())));
    layOutChain();

    stripViewport.setBounds (area);

    const auto contentWidth = juce::jmax (stripViewport.getMaximumVisibleWidth(),
                                          strips.size() * stripWidth);
    stripHolder.setSize (contentWidth, stripViewport.getMaximumVisibleHeight());

    auto holder = stripHolder.getLocalBounds();

    for (auto* strip : strips)
        strip->setBounds (holder.removeFromLeft (stripWidth));
}

} // namespace dew
