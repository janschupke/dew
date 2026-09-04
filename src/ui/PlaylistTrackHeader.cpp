#include "ui/PlaylistTrackHeader.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
#include "ui/design/Cursors.h"

namespace dew
{

using namespace tokens;

PlaylistTrackHeader::PlaylistTrackHeader (ProjectDocument& d, juce::ValueTree t)
    : document (d)
    , track (std::move (t))
{
    setComponentID ("playlistTrackHeader");
    muteButton.setComponentID ("trackMute");
    soloButton.setComponentID ("trackSolo");

    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    nameLabel.setEditable (false, true, false);

    // setEditable does not stop a Label eating clicks - it only touches
    // keyboard focus - so without this the label swallows every press across
    // the header's whole left side and the row's own mouseDown never runs.
    // The channel rack learned this the hard way; see the README.
    nameLabel.setInterceptsMouseClicks (false, false);
    nameLabel.setFont (type::font (type::body));
    nameLabel.setColour (juce::Label::textColourId, colour::textPrimary);
    nameLabel.onTextChange = [this]
    {
        ProjectEdits::setProperty (track, ids::name, nameLabel.getText(),
                                   &document.getUndoManager(), "Rename track");
    };
    addAndMakeVisible (nameLabel);

    muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    muteButton.onClick = [this]
    {
        ProjectEdits::setProperty (track, ids::mute, muteButton.getToggleState(),
                                   &document.getUndoManager(), "Mute track");
    };
    addAndMakeVisible (muteButton);

    soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    soloButton.onClick = [this]
    {
        ProjectEdits::setProperty (track, ids::solo, soloButton.getToggleState(),
                                   &document.getUndoManager(), "Solo track");
    };
    addAndMakeVisible (soloButton);
}

juce::PopupMenu PlaylistTrackHeader::buildMenu() const
{
    juce::PopupMenu menu;
    menu.addItem ((int) MenuItem::rename, "Rename");
    colourMenu::addTo (menu, track, colourBaseId);
    menu.addItem ((int) MenuItem::addTrack, "Add track");
    menu.addSeparator();

    // Here as well as on the toolbar and on alt-0, because this is the menu
    // you are already in when a drag on the edge above went too far.
    menu.addItem ((int) MenuItem::resetHeight, "Reset track height");
    menu.addSeparator();
    menu.addItem ((int) MenuItem::removeTrack, "Remove track");
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
    muteButton.setComponentID ("trackMute");
    soloButton.setComponentID ("trackSolo");

    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    repaint();
}

void PlaylistTrackHeader::paint (juce::Graphics& g)
{
    g.fillAll (colour::surface);

    // A muted track's whole header dims, so the state is readable from
    // across the arrangement and not only from the letter.
    if ((bool) track[ids::mute])
    {
        g.setColour (colour::wellDeep.withAlpha (emphasis::subdued));
        g.fillAll();
    }

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
    // 200px header stops pointing at anything. Toggles stretched to 200px
    // are also not toggles.
    auto row = area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                   .reduced (space::sm, space::xs);

    // Past the roomy threshold the name gets a line of its own and the
    // toggles drop below it. One row of controls with a void under it is
    // what a tall header looks like otherwise, and the name is the thing
    // there is finally room to read.
    const auto roomy = getHeight() >= size::trackHeightRoomy;

    auto toggles = roomy ? area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                               .reduced (space::sm, space::xs)
                         : row;

    soloButton.setBounds (toggles.removeFromRight (size::letterToggle).reduced (0, space::xxs));
    toggles.removeFromRight (space::xxs);
    muteButton.setBounds (toggles.removeFromRight (size::letterToggle).reduced (0, space::xxs));
    toggles.removeFromRight (space::sm);

    nameLabel.setBounds (roomy ? row : toggles);
}

} // namespace dew
