#include "ui/PlaylistTrackHeader.h"

#include "i18n/Strings.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/ColourMenu.h"
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

    // onModifiedClick rather than onClick, because shift IS the gesture here.
    // Every lane takes the state this one just took, as ONE undo step - which
    // is how "silence everything but that" is asked for now that a lane has no
    // solo to say it with.
    enabledButton.onModifiedClick = [this] (const juce::ModifierKeys& mods)
    {
        const auto muted = enabledButton.getToggleState();

        if (mods.isShiftDown())
        {
            ProjectEdits::setPropertyOnEvery (track.getParent(), ids::PLAYLIST_TRACK, ids::mute,
                                              muted, &document.getUndoManager(),
                                              muted ? "Silence every track" : "Play every track");
            return;
        }

        ProjectEdits::setProperty (track, ids::mute, muted, &document.getUndoManager(),
                                   muted ? "Silence track" : "Play track");
    };
    addAndMakeVisible (enabledButton);
}

juce::PopupMenu PlaylistTrackHeader::buildMenu() const
{
    juce::PopupMenu menu;
    addGlyphItem (menu, (int) MenuItem::rename, tr (StringId::playlist_menu_rename),
                  glyph::forAction (glyph::Action::rename));
    colourMenu::addTo (menu, track, colourBaseId);
    addGlyphItem (menu, (int) MenuItem::addTrack, tr (StringId::playlist_menu_addTrack),
                  glyph::forAction (glyph::Action::add));
    menu.addSeparator();

    // Here as well as on the toolbar and on alt-0, because this is the menu
    // you are already in when a drag on the edge above went too far.
    addGlyphItem (menu, (int) MenuItem::resetHeight, tr (StringId::playlist_menu_resetHeight),
                  glyph::forAction (glyph::Action::reset));
    menu.addSeparator();
    addGlyphItem (menu, (int) MenuItem::removeTrack, tr (StringId::playlist_menu_removeTrack),
                  glyph::forAction (glyph::Action::remove));
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
    enabledButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
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
    // 200px header stops pointing at anything. A toggle stretched to 200px
    // is also not a toggle.
    auto row = area.removeFromTop (juce::jmin (area.getHeight(), size::rowHeight))
                   .reduced (space::sm, space::xs);

    // The indicator leads the row and the name follows it, on ONE rung at every
    // lane height. There used to be a second rung past the roomy threshold,
    // holding the two toggles under the name; with one indicator there is
    // nothing to put on it, and a state that moved to a different row as the
    // lane grew was a state you had to look for twice.
    enabledButton.setBounds (row.removeFromLeft (size::letterToggle).reduced (0, space::xxs));
    row.removeFromLeft (space::sm);

    nameLabel.setBounds (row);
}

} // namespace dew
