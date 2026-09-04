#include "ui/MixerComponent.h"

#include "model/ProjectEdits.h"
#include "ui/MenuSeam.h"
#include "ui/design/Tokens.h"

#include "model/EntityColour.h"
#include "model/Ids.h"

namespace dew
{

using namespace tokens;

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
            auto* strip = strips.add (new MixerStrip (document, track, false));
            strip->attachParamMenus (paramMenuHost);
            const auto id = (int) track[ids::id];
            strip->onSelected = [this, id] { editorState.setSelectedMixerTrackId (id); };
        }

    // Master had no onSelected at all, so clicking it did nothing and its chain
    // could never be edited. It selects like any other strip now.
    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        auto* strip = strips.add (new MixerStrip (document, master, true));
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

MixerStrip* MixerComponent::stripFor (int mixerTrackId) const
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
