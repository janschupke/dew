#include "ui/PlaylistTrackHeader.h"

#include "i18n/Strings.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/RowSilence.h"
#include "ui/design/Glyphs.h"
#include "ui/design/MenuGlyph.h"
#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;

PlaylistTrackHeader::PlaylistTrackHeader (ProjectDocument& d, juce::ValueTree t)
    : document (d)
    , track (std::move (t))
{
    setComponentID ("playlistTrackHeader");
    enabledButton.setComponentID ("trackEnabled");

    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    nameLabel.setEditable (false, true, false);

    // One track is one group; refresh() repeats it for a rename.
    setTitle (nameLabel.getText());
    setFocusContainerType (FocusContainerType::focusContainer);

    // setEditable does not stop a Label eating clicks - it only touches
    // keyboard focus - so without this the label swallows every press across
    // the header's whole left side and the row's own mouseDown never runs.
    // The channel rack learned this the hard way; see the README.
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (type::font (type::body));
    nameLabel.onTextChange = [this]
    {
        ProjectEdits::setProperty (track, ids::name, nameLabel.getText(),
                                   &document.getUndoManager(), "Rename track");
    };
    addAndMakeVisible (nameLabel);

    enabledButton.setClickingTogglesState (true);
    enabledButton.setOnColour (colour::warning);
    enabledButton.setTooltip (tr (StringId::playlist_enabled_help));
    enabledButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);

    // Guarded, for the reason the rack's refresh is: setValue fires
    // onValueChange, and writing the value back into the document from here
    // would open an undo step for every repaint.
    {
        const juce::ScopedValueSetter<bool> quiet (updating, true);
        volumeKnob.setValue ((double) track[ids::gain], juce::dontSendNotification);
    }

    // onModifiedClick rather than onClick, because shift IS the gesture here.
    // Every lane takes the state this one just took, as ONE undo step - which
    // is how "silence everything but that" is asked for now that a lane has no
    // solo to say it with.
    enabledButton.onModifiedClick = [this] (const juce::ModifierKeys& mods)
    {
        const auto muted = enabledButton.getToggleState();

        if (mods.isShiftDown())
        {
            ProjectEdits::setPropertyOnEvery (
                track.getParent(), ids::PLAYLIST_TRACK, ids::mute, muted,
                &document.getUndoManager(), muted ? "Turn every track off" : "Turn every track on");
            return;
        }

        ProjectEdits::setProperty (track, ids::mute, muted, &document.getUndoManager(),
                                   muted ? "Turn track off" : "Turn track on");
    };
    addAndMakeVisible (enabledButton);

    // Volume on the lane itself, so an arrangement can be balanced without
    // reaching for whichever channel happens to be playing on it. Same range
    // and same compact treatment as a rack row's; no caption fits on a 34px
    // row there either.
    volumeKnob.setCompact (true);
    volumeKnob.setTooltip (tr (StringId::playlist_volume_help));
    volumeKnob.setValue ((double) track[ids::gain], juce::dontSendNotification);

    // The first value of a drag opens the transaction and the rest join it; a
    // wheel or a keypress is not part of a drag and opens its own. A rack row
    // says this the same way, for the same reason - and now through the same
    // object.
    gesture.attach (volumeKnob,
                    [this] (bool continuing)
                    {
                        if (updating)
                            return false;

                        ProjectEdits::setProperty (track, ids::gain, volumeKnob.getValue(),
                                                   &document.getUndoManager(),
                                                   "Change track volume", continuing);
                        return true;
                    });

    addAndMakeVisible (volumeKnob);
}

juce::PopupMenu PlaylistTrackHeader::buildMenu() const
{
    juce::PopupMenu menu;
    addGlyphItem (menu, (int) MenuItem::rename, tr (StringId::playlist_menu_rename),
                  glyph::Action::rename);
    colourMenu::addTo (menu, track, colourBaseId);
    addGlyphItem (menu, (int) MenuItem::addTrack, tr (StringId::playlist_menu_addTrack),
                  glyph::Action::add);
    menu.addSeparator();

    // Here as well as on the toolbar and on alt-0, because this is the menu
    // you are already in when a drag on the edge above went too far.
    addGlyphItem (menu, (int) MenuItem::resetHeight, tr (StringId::playlist_menu_resetHeight),
                  glyph::Action::reset);
    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::removeTrack, tr (StringId::playlist_menu_removeTrack),
                  glyph::Action::remove);
    return menu;
}

void PlaylistTrackHeader::applyMenuChoice (int choice)
{
    if (colourMenu::apply (choice, track, colourBaseId, document))
        return;

    switch ((MenuItem) choice)
    {
        case MenuItem::rename: nameLabel.showEditor(); break;
        case MenuItem::addTrack:
            if (onAddTrack)
                onAddTrack();
            break;
        case MenuItem::removeTrack:
            if (onRemoveTrack)
                onRemoveTrack (track);
            break;
        case MenuItem::resetHeight:
            if (onResetHeight)
                onResetHeight();
            break;
        default: break;
    }
}

// --- the resize grip ---------------------------------------------------------

void PlaylistTrackHeader::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (isOnResizeEdge (event.getPosition()) ? cursor::value : cursor::idle);
}

void PlaylistTrackHeader::mouseDrag (const juce::MouseEvent& event)
{
    // SCREEN coordinates, and a delta rather than a position. This header's
    // own height is what the drag is changing, so its local frame moves
    // under the pointer while the pointer is being read in it - and
    // getDistanceFromDragStart asks the mouse SOURCE, which no synthetic
    // event ever pressed, so it is always zero in a test.
    if (resizing && onResizeDrag)
        onResizeDrag (index, event.getScreenPosition().y - resizeOriginY);
}

void PlaylistTrackHeader::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (resizing, false) && onResizeEnd)
        onResizeEnd();
}

bool PlaylistTrackHeader::consumePress (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || ! isOnResizeEdge (event.getPosition()))
        return false;

    resizing = true;
    resizeOriginY = event.getScreenPosition().y;

    if (onResizeBegin)
        onResizeBegin();

    return true;
}

// --- state and painting ------------------------------------------------------

void PlaylistTrackHeader::refresh()
{
    setComponentID ("playlistTrackHeader");
    enabledButton.setComponentID ("trackEnabled");

    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    setTitle (nameLabel.getText());
    enabledButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);

    // The name and the power glyph are CHILDREN, and a child paints after its
    // parent - so the scrim above never reached either of them.
    silence::applyTo (*this, (bool) track[ids::mute], &enabledButton);

    repaint();
}

void PlaylistTrackHeader::paint (juce::Graphics& g)
{
    g.fillAll (colour::surface);

    // A band down the whole left edge, not a tab beside the name.
    //
    // It is what stops a tall header being a short row with a hole under it,
    // and it costs nothing: a band scales to any height by construction.
    //
    // The track's own colour if it has chosen one, and otherwise its
    // POSITION in the list - which is what every lane did before a lane
    // could carry one, so a project that has never set one looks exactly as
    // it did, and moving a lane still recolours it.
    g.setColour (laneColour());
    g.fillRect (0, 0, colourTabWidth, getHeight());

    g.setColour (colour::divider);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Last, so the lane's colour band dims with everything else. The scrim was
    // painted BEFORE the band, which left the brightest thing on the row the
    // one belonging to the track that is not playing.
    silence::paintOver (g, getLocalBounds(), (bool) track[ids::mute]);
}

juce::Colour PlaylistTrackHeader::laneColour() const
{
    return entityColour::stored (track).value_or (tokens::colour::channelColour (index));
}

void PlaylistTrackHeader::setIndex (int newIndex)
{
    if (std::exchange (index, newIndex) != newIndex)
        repaint();
}

void PlaylistTrackHeader::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (colourTabWidth);

    // The row keeps its OWN height, at the top. It does not stretch and it
    // does not centre: a track's name labels the lane's first pixel, which
    // is where its clips begin, and a name that drifts to the middle of a
    // 200px header stops pointing at anything. A toggle stretched to 200px
    // is also not a toggle.
    auto row = area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                   .reduced (space::sm, space::xs);

    // Name, volume, on/off - the order a channel rack row reads in, laid out
    // right to left so the name takes what is left. A lane and a channel are
    // the two things a song is balanced with and they read the same way now;
    // they used to be mirror images, the lane leading with its indicator and
    // the row trailing with it, so the same three facts sat in the opposite
    // order an inch apart.
    //
    // Still ONE rung at every lane height. There used to be a second rung past
    // the roomy threshold holding the toggles under the name, and a state that
    // moved to a different row as the lane grew was a state you had to look
    // for twice.
    enabledButton.setBounds (row.removeFromRight (size::letterToggle).reduced (0, space::xxs));
    row.removeFromRight (space::xs);

    volumeKnob.setBounds (
        row.removeFromRight (size::knobSm).withSizeKeepingCentre (size::knobSm, size::knobSm));
    row.removeFromRight (space::sm);

    nameLabel.setBounds (row);
}

} // namespace dew
