#include "ui/MixerComponent.h"

#include <utility>

#include "i18n/Strings.h"
#include "model/ProjectEdits.h"
#include "ui/MenuSeam.h"
#include "ui/design/Tokens.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/TreeWalk.h"

namespace dew
{

using namespace tokens;

// -----------------------------------------------------------------------------

void MixerComponent::setPresetHoverSink (std::function<void (const juce::String&)> sink)
{
    chainHost.setPresetHoverSink (std::move (sink));
}

void MixerComponent::setParamMenuHost (const paramMenu::Host* host)
{
    paramMenuHost = host;

    // The strips are built before the host arrives, so this re-attaches rather
    // than only recording the pointer for the next rebuild.
    forEachStrip ([host] (auto* strip) { strip->attachParamMenus (host); });
}

MixerComponent::MixerComponent (ProjectDocument& d, EditorState& s, AudioEngine* e)
    : document (d)
    , editorState (s)
    , engine (e)
    , chainHost (d, s, EffectChainHost::Orientation::horizontal)
{
    setComponentID ("mixer");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::mixer_title));
    setFocusContainerType (FocusContainerType::focusContainer);

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
    addStripButton.setTooltip (tr (StringId::mixer_addInsert_help));
    addStripButton.onClick = [this] { addMixerTrack(); };
    stripHolder.addAndMakeVisible (addStripButton);

    if (engine != nullptr)
        startTimerHz (tokens::motion::uiRefreshHz);

    // The band's own top rule is a grip. The host reports the gesture and this
    // owns the height, the division PlaylistTrackHeader keeps with the playlist.
    chainHost.onResizeBegin = [this] { heightAtDragStart = bandHeight(); };

    // Dragging UP makes the band taller, because that is the direction the band
    // grows in. Computed from where the press was rather than accumulated, so
    // two routes to the same pointer position give the same band - and in
    // PIXELS, so the band follows the pointer the way every other drag area in
    // dew does. It used to round the travel to the nearest knob row, which gave
    // the drag four reachable positions 68px apart.
    chainHost.onResizeDrag = [this] (int deltaY)
    { setEffectBandHeight (heightAtDragStart - deltaY); };

    // The one host that never wired this. A chain that changed size just waited
    // for the next resized() to notice, which is fine until the band's height is
    // something the user chose and the cards reflow inside it.
    chainHost.onPreferredHeightChanged = [this] { resized(); };

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
    masterStrip.reset();

    if (const auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        masterStrip = std::make_unique<MixerStrip> (document, master, true);
        masterStrip->attachParamMenus (paramMenuHost);
        masterStrip->onSelected = [this] { editorState.setSelectedMixerTrackId (masterTrackId); };
    }

    // Both edits belong to the mixer, not to the strip the menu opened on: that
    // strip is deleted by the rebuild either one causes.
    forEachStrip (
        [this] (auto* strip)
        {
            strip->onAddInsert = [this] { addMixerTrack(); };
            strip->onRemoveInsert = [this] (int id) { removeMixerTrack (id); };
        });

    // The inserts scroll; the master does not. That is the whole of the
    // difference, and it is a difference of PARENT rather than of position.
    for (auto* strip : strips)
        stripHolder.addAndMakeVisible (strip);

    if (masterStrip != nullptr)
        addAndMakeVisible (*masterStrip);

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

    // A rule between the pinned master and the inserts that scroll past it.
    // Without one the master reads as the first insert rather than as the bus
    // they all arrive at, which is the whole reason it is pinned.
    if (! masterSeam.isEmpty())
    {
        g.setColour (tokens::colour::dividerStrong);
        g.fillRect (
            masterSeam.withWidth (tokens::stroke::hairlinePx).withX (masterSeam.getCentreX()));
    }
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
        selectedTrack = tree::childWithId (mixer, ids::MIXER_TRACK, selectedId);

    chainHost.setOwner (selectedTrack, ! selectedTrack.isValid() ? juce::String()
                                       : selectedId == masterTrackId
                                           ? "Master"
                                           : selectedTrack[ids::name].toString());

    forEachStrip (
        [selectedId] (auto* strip)
        {
            strip->setSelected (strip->isMasterStrip() ? selectedId == masterTrackId
                                                       : strip->getTrackId() == selectedId);
        });
}

void MixerComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pointChainAtSelectedTrack();
}

void MixerComponent::timerCallback()
{
    if (engine == nullptr)
        return;

    // The index IS the insert index now. It used to be the array position with
    // the master sitting at the end of it, which happened to agree only because
    // the master was last.
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setLevel (engine->readAndClearTrackPeak (i));

    if (masterStrip != nullptr)
        masterStrip->setLevel (engine->readAndClearMasterPeak());
}

void MixerComponent::updateRouting()
{
    // Which channels feed each insert. Without this the mixer is a row of
    // anonymous faders and there is nothing to say what any of them carries.
    forEachStrip (
        [this] (auto* strip)
        {
            juce::Array<juce::var> names;
            juce::Array<juce::Colour> colours;
            juce::Array<int> channelIds;

            // Nothing is routed INTO the master by a channel's mixerTrackId -
            // everything reaches it through an insert - so it is given an empty
            // list rather than skipped, which is what clears one if a strip
            // ever stops being the master it was built as.
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
        });
}

int MixerComponent::bandHeight() const
{
    // 0 is "never set", so the band opens at the depth it has always opened at
    // and nothing re-flows on upgrade.
    return effectBandHeight > 0 ? effectBandHeight
                                : chainHost.bandHeightForRows (size::effectBandRowsDefault);
}

void MixerComponent::setEffectBandHeight (int pixels)
{
    const auto clamped = juce::jlimit (chainHost.bandHeightForRows (size::effectBandRowsMin),
                                       chainHost.bandHeightForRows (size::effectBandRowsMax),
                                       pixels);

    if (std::exchange (effectBandHeight, clamped) == clamped)
        return;

    resized();
}

void MixerComponent::setEffectBandRows (int rows)
{
    setEffectBandHeight (chainHost.bandHeightForRows (
        juce::jlimit (size::effectBandRowsMin, size::effectBandRowsMax, rows)));
}

int MixerComponent::getEffectBandRows() const noexcept
{
    return chainHost.getKnobRows();
}

void MixerComponent::resized()
{
    auto area = getLocalBounds();

    // The chain row gets the bottom of the panel at the depth the band has been
    // dragged to: strips need the rest and a fader is useless once it is
    // shorter than a thumb, so the band never takes more than half.
    //
    // The HEIGHT is a pixel count and the CARDS keep whole rows. The band grows
    // continuously under the pointer and gains a row of knobs whenever one
    // more will fit, which is what makes the few pixels between two rungs band
    // ground rather than a knob standing on half a row.
    //
    // Edge to edge, and flush against the strips. It used to be inset on all
    // four sides and then trimmed again at the top, so the row was a rounded
    // card floating on the window background with nothing joining it to the
    // strip it belongs to. It is a BAND now - its own ground, with a rule along
    // its top, which is what the host paints.
    const auto depth = juce::jmin (bandHeight(), area.getHeight() / 2 - tokens::space::md);

    // `depth` exactly, with nothing added. It used to be given depth + space::md
    // while the ROWS were computed from depth alone, so the band was always
    // eight pixels taller than the number of knob rows it had chosen to hold -
    // and bandHeightForRows, which knobRowsFitting inverts by walking, was no
    // longer describing the band anyone saw.
    chainHost.setKnobRows (chainHost.knobRowsFitting (depth));
    chainHost.setBounds (area.removeFromBottom (depth));

    // The strips keep the inset. They are objects on the window's ground; the
    // band below them is a region OF it.
    auto stripArea = area.reduced (space::md);

    // The master, PINNED to the left, outside the viewport that scrolls.
    //
    // Every signal in the project passes through it, so it is the one strip
    // that has to be reachable from wherever the row happens to be scrolled -
    // and at twenty inserts the row is always scrolled. It sat at the far right
    // of the holder before, which put it furthest from the inserts a person
    // scrolls to and off the screen entirely at any useful width.
    //
    // Left rather than right because a mixer is read left to right and the
    // master is where the signal ends up: keeping it in view means keeping the
    // destination in view, and the leading edge is the one that never moves.
    if (masterStrip != nullptr)
    {
        masterStrip->setBounds (stripArea.removeFromLeft (size::mixerStripWidth));
        masterSeam = stripArea.removeFromLeft (space::md).withTrimmedTop (0);
    }
    else
        masterSeam = {};

    stripViewport.setBounds (stripArea);

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

    // The head of the next column, at the strip's own inset, filling its width
    // - which is what "+ Track" does in the playlist's next empty ROW and what
    // "+ Channel" does in the rack's. It was a 24px square centred across a
    // 72px column, so the one add button in the app with no noun on it was also
    // the only one that did not line up with anything: a glyph dropped in a gap
    // rather than an offer at the head of a column.
    auto column = holder.removeFromLeft (size::mixerStripWidth).reduced (space::sm, space::md);

    // The button's own height. It was controlHeightSm - a 20px button, the only
    // one in the application, in a column of controls that are all 26.
    addStripButton.setBounds (column.removeFromTop (addStripButton.preferredHeight()));
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
    request.title = tr (StringId::dialog_removeInsert_title);
    request.message = tr (StringId::dialog_removeInsert_body,
                          Args {}.with ("name", track[ids::name].toString()));
    request.confirmText = tr (StringId::dialog_removeInsert_confirm);

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
    MixerStrip* found = nullptr;

    forEachStrip (
        [&found, mixerTrackId] (auto* strip)
        {
            if (found == nullptr && strip->getTrackId() == mixerTrackId)
                found = strip;
        });

    return found;
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
