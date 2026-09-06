#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

#include "app/ProjectDocument.h"
#include "ui/HeaderRow.h"
#include "ui/design/Icons.h"
#include "ui/primitives/DewControls.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** One track's header at the left of the playlist: its name, mute, solo, a
    colour band, and the grip along its bottom edge that sets the lane height.

    A nested class of PlaylistComponent until it was a quarter of that file.
    Promoted rather than split into a second translation unit, which is what
    the paint code got: this reads none of the playlist's members. It is handed
    a document and a track and reports everything else through callbacks, the
    same shape PlaylistToolbar has and for the same reason - it can be laid out
    and driven in a test with no arrangement behind it.

    The twin is ChannelRackComponent::ChannelHeader. Both derive from HeaderRow,
    which is where what a header DOES lives; what they hold and what their menus
    say is their own.
*/
class PlaylistTrackHeader : public HeaderRow
{
public:
    PlaylistTrackHeader (ProjectDocument&, juce::ValueTree);

    enum class MenuItem
    {
        rename = 1,
        addTrack,
        removeTrack,
        resetHeight
    };

    /** Where the colour submenu's ids start: after this row's own, so the two
        numberings cannot collide. */
    static constexpr int colourBaseId = (int) MenuItem::resetHeight + 1;

    juce::PopupMenu buildMenu() const override;
    void applyMenuChoice (int choice) override;

    /** Add and remove belong to the playlist, which owns the list of tracks. */
    std::function<void()> onAddTrack;
    std::function<void (juce::ValueTree)> onRemoveTrack;

    /** So does the height: every lane shares one. */
    std::function<void()> onResetHeight;

    juce::ValueTree getTrack() const
    {
        return track;
    }

    // --- the resize grip -----------------------------------------------------
    /** How deep the grab band along the bottom edge is.

        A row of its own would be a control; this is an edge, and an edge has to
        be thin enough that the rest of the header is still a header. Four
        pixels is the smallest thing dew's spacing scale names, and it is what
        the pointer shape is for.
    */
    static constexpr int resizeBandHeight = tokens::space::xs;

    bool isOnResizeEdge (juce::Point<int> p) const
    {
        return p.y >= getHeight() - resizeBandHeight;
    }

    /** Latched, dragged, released. The playlist owns the height - every lane
        shares one - so all three of these are reports rather than edits. */
    std::function<void()> onResizeBegin;
    std::function<void (int laneIndex, int deltaY)> onResizeDrag;
    std::function<void()> onResizeEnd;

    /** The band's width. Named rather than a bare 4 in a fillRect, and matched
        to the tab the channel rack's own rows draw. */
    static constexpr int colourTabWidth = 4;

    void refresh();

    /** What this lane is painted in: its own colour, or the ramp entry for its
        position when it has not been given one. */
    juce::Colour laneColour() const;

    /** Which row this is, for the colour band. Re-set by rebuildHeaders, which
        already rebuilds every header whenever the list changes. */
    void setIndex (int newIndex);

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label* editableLabel() override
    {
        return &nameLabel;
    }

    bool consumePress (const juce::MouseEvent&) override;

    ProjectDocument& document;
    juce::ValueTree track;
    int index = 0;

    juce::Label nameLabel;

    /** Whether the lane plays, and the only such state it has.

        It was an M and an S. Two indicators for one question is what made solo
        necessary in the first place - a mute that could not say "and silence
        the others" needed a second flag that could - and the cost was a lane
        whose audibility could not be read off the lane. One control, one state,
        and shift-click says it of every lane at once.

        A glyph rather than a letter: "M" names the CONTROL and this one names
        the state, and the crossed speaker is what dew already draws for
        silence. It lights when the lane is off, the way every toggle in dew
        lights for the state worth noticing.
    */
    DewIconButton enabledButton { icons::power(), {} };

    /** How loud this lane is.

        The same control the channel rack's row carries, in the same order
        relative to the name and the on/off - a lane and a channel are the two
        things you balance a song with, and one of them had no way to say it.

        There is no PAN beside it, and that is a fact about the engine rather
        than a gap in the row: pan is applied once per channel and once per
        mixer track, and by the time a signal reaches either, the notes every
        lane contributed are already summed into one buffer. Volume works
        because it is expressible on the TRIGGER - see ClipSnapshot::trackGain.
    */
    DewKnob volumeKnob { requireInstrumentParamSpec (ids::volume) };

    /** True while refresh() is writing values into controls, so a control's own
        callback does not write them straight back into the document. */
    bool updating = false;

    /** True between a knob's onEditStart and onEditEnd, and whether a
        transaction is already open for that drag - see the volume knob's
        onValueChange. The rack's rows carry the same pair for the same reason. */
    bool inDrag = false;
    bool gestureActive = false;

    bool resizing = false;
    int resizeOriginY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistTrackHeader)
};

} // namespace dew
