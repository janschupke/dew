#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/NoteTools.h"
#include "app/ProjectDocument.h"
#include "ui/CanvasCursor.h"
#include "ui/EditorState.h"
#include "ui/PianoRollToolbar.h"
#include "ui/TimelineRuler.h"
#include "ui/TimelinePaint.h"
#include "ui/TimelineView.h"
#include "ui/RowView.h"
#include "ui/design/Tokens.h"

namespace dew
{

/** Note editing with pitch, over a scrollable and zoomable timeline.

    Edits the same notes the step grid does. A note written here at a pitch the
    channel does not use still lights that step in the rack, because there is one
    representation rather than two.

    Layout, all four parts driven by the same TimelineView so they cannot drift:

        toolbar                                |
        corner   | ruler                       |
        keyboard | notes                       | v scrollbar
        VEL      | velocity lane               |
                 | h scrollbar                 |

    The tool decides only what an unmodified press on the grid means. Every
    modifier gesture means the same thing whichever tool is current, so the
    muscle memory for erasing or rubber-banding does not depend on a mode:

        right/alt-drag       erase the notes swept over
        cmd/ctrl-drag empty  rubber-band select
        shift-click a note   add to or remove from the selection
        drag in the lane     set velocity
        click the keyboard   audition
        click/drag the ruler move the playhead

    By tool:
        select  click empty adds a note at the last length and velocity used,
                and the same drag sets its length; click a note to select it,
                drag to move the selection, drag its right edge to resize
        paint   drag writes a note in every grid cell crossed, skipping cells
                that already hold one; pressing an existing note still moves it
        slice   drag a line across notes to cut each one where it crosses

    Snapping applies to adding, moving and resizing. Holding shift suspends it
    for the length of a drag. At the finest division every snapped expression is
    the identity, so the roll behaves exactly as it did before the grid existed.

    Keys:
        delete / backspace   delete the selection
        cmd/ctrl-A           select every note on this channel
        1 / 2 / 3            select, paint, slice
        up / down            transpose a semitone; shift for an octave
        Q / R                quantize; open the randomize dialog
        cmd/ctrl-wheel       zoom around the pointer
        shift-wheel          scroll horizontally

    Quantize, transpose and randomize act on the selection when there is one and
    on the whole channel otherwise - see NoteTools::scopeFor.
*/
class PianoRollComponent : public juce::Component,
                           private juce::Timer,
                           private juce::ValueTree::Listener,
                           private juce::ChangeListener,
                           private juce::ScrollBar::Listener
{
public:
    PianoRollComponent (ProjectDocument&, AudioEngine&, EditorState&);
    ~PianoRollComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    void refresh();

    /** Frames the pattern: all of it visible, scrolled to its notes. */
    void zoomToFit();

    /** Scrolls the selected channel's notes into view, but only when none of
        them are already visible - re-framing a view you can already work in is
        just the editor moving under your hands.
    */
    void scrollToNotesIfOffscreen();

    /** Scrolls so this pitch sits in the middle of the note area. */
    void centreOnPitch (int pitch);

    // --- geometry, public so a test can aim at a note instead of recomputing
    //     the layout and drifting from it -------------------------------------
    const TimelineView& getTimeline() const noexcept
    {
        return timeline;
    }
    int getNumSelectedNotes() const noexcept
    {
        return selection.size();
    }

    /** Where a note is painted, in this component's coordinates. */
    juce::Rectangle<float> getBoundsForNote (const juce::ValueTree& note) const
    {
        return boundsForNote (note);
    }

    juce::Rectangle<int> getNoteArea() const
    {
        return noteArea();
    }

    /** Which pitch a y coordinate lands on. Public for the same reason the
        areas are: a test asserting the view did not walk has to ask what is
        under a point, not recompute the mapping and drift from it. */
    int getPitchAtY (int y) const
    {
        return pitchAtY (y);
    }
    juce::Rectangle<int> getKeyboardArea() const
    {
        return keyboardArea();
    }
    juce::Rectangle<int> getVelocityArea() const
    {
        return velocityArea();
    }
    juce::Rectangle<int> getVelocityResizeArea() const
    {
        return velocityResizeArea();
    }
    juce::Rectangle<int> getRulerArea() const
    {
        return rulerArea();
    }
    juce::Rectangle<float> getVelocityBarBounds (const juce::ValueTree& note) const
    {
        return velocityBarBounds (note);
    }

    /** The pitch currently being auditioned, or -1. */
    int getAuditionPitch() const noexcept
    {
        return auditionPitch;
    }

    /** How hard a press at `x` hits, across the keyboard gutter.

        The near edge is quiet and the far edge is full, which is how a keyboard
        with no aftertouch has always been played - and it is what the gutter's
        54 pixels were doing with nothing at all before: the x of the press was
        available at the call site and thrown away one line later, so every key
        sounded at whatever velocity the roll had last DRAWN a note with, a
        number a person auditioning a sound cannot change without drawing
        something first.

        Static and public because it is the whole rule, and pure: a test can
        pin the curve without a window, a channel or an engine behind it.
    */
    static float auditionVelocityForX (int x) noexcept;

    /** Where the view is, so a session can be restored to it. */
    void captureView (double& zoom, double& scroll, double& pitchScroll) const;
    void applyView (double zoom, double scroll, double pitchScroll);

    // --- vertical size -------------------------------------------------------
    /** How tall one pitch row is.

        The roll's vertical axis is the only one in dew that carries MATERIAL
        rather than a list: a lane is a track and a rack row is a channel, but
        a row here is a semitone, and how many of them you want on screen
        depends on whether you are writing a melody or reading a voicing.

        One height for every row, for the reason the playlist gives for lanes:
        a view where the same gesture works or does not depending on which
        octave you aim at is worse than one that is uniformly too dense.
    */
    int getRowHeight() const noexcept
    {
        return rows.height;
    }
    /** Real rather than whole, for the reason RowView::exactHeight gives: a
        wheel notch asks for a fraction of a pixel and the fraction has to reach
        the next event. Every other caller hands it a whole one. */
    void setRowHeight (double);

    /** Multiplies the row height, or fits every pitch that has a note in it
        when `factor` is 0 - the shape ZoomButtons reports. */
    void zoomRowsBy (double factor);

    void fitRowsToWindow();

    /** How tall the velocity lane is, in pixels.

        On the VIEW, which is the rule PlaylistComponent's lane height states:
        it is not part of the music, it must not dirty a project or land on the
        undo stack, and EditorState is a ChangeBroadcaster whose other listeners
        have no interest in it. The precedent beside this one is pitchScrollPx.
    */
    int getVelocityHeight() const noexcept
    {
        return velocityLaneHeight;
    }

    /** Clamped to the token range, and to whatever the window can spare while
        still leaving a row to write notes in. Takes a negative: a drag computes
        its height by subtraction. "0 means leave the default" is the settings
        contract and lives in EditorTabs, beside the playlist's. */
    void setVelocityHeight (int height);

    /** The snap grid, persisted between launches. The tool deliberately is not:
        opening into Paint or Slice means the first click of a session writes or
        cuts something, which is not a state to restore someone into.
    */
    SnapDivision getSnap() const noexcept
    {
        return toolbar.getSnap();
    }
    void setSnap (SnapDivision snap)
    {
        toolbar.setSnap (snap);
    }

    RollTool getTool() const noexcept
    {
        return toolbar.getTool();
    }
    void setTool (RollTool tool)
    {
        toolbar.setTool (tool);
    }

    /** The tool strip, so a test can drive the channel selector and the zoom
        buttons through the same seam the user reaches them by.
    */
    PianoRollToolbar& getToolbar() noexcept
    {
        return toolbar;
    }

    /** The notes an edit acts on: the selection when there is one, otherwise
        every note of the current channel.
    */
    juce::Array<juce::ValueTree> editScope() const;

    void quantizeScope();
    void transposeScope (int semitones);

    /** Where one step of one pitch sits, whether or not a note is there. */
    juce::Rectangle<float> boundsForCell (int step, int pitch) const;

    /** Where the arrow keys are, as {step, pitch}. See CanvasCursor. */
    bool moveCursor (juce::Point<int> delta);
    void activateCursor();
    void announceCursor();
    juce::Rectangle<int> cursorLimits() const;

    CanvasCursor cursor;
    void openRandomizeDialog();

    /** Where the transport is inside the pattern this roll is showing, in that
        pattern's own steps - or nothing, when it is not inside it at all.

        In PATTERN mode the transport is the pattern, so it is the position
        wrapped by the pattern's length. In SONG mode the roll shows a pattern
        the arrangement may or may not be playing right now, and the roll used
        to answer that by drawing nothing ever: the indicator was gated on
        pattern mode outright, so following a song into the pattern being edited
        was something you could only do by switching modes and losing your
        place. The answer is the position inside whichever clip of THIS pattern
        the song playhead is in, and nothing when it is in none of them.

        A double rather than a step index. Every pattern-mode view truncated to
        the integer step, which is why the line ticked in the roll and glided in
        the playlist - the playlist was the only one that kept the fraction.

        Public because a test reads it: what this answers is the whole of the
        behaviour, and asserting it in pixels would be asserting the painter.
    */
    std::optional<double> playheadInPattern() const;

private:
    // Scrubbing and range-selecting are not here: the ruler's whole gesture
    // lives in ruler::Gesture, which the playlist and the channel rack share.
    enum class Gesture
    {
        none,
        moving,

        /** An existing note's RIGHT EDGE. Dragging left past the note's own
            start shortens it to one division and stops, which is what a right
            edge should do. */
        resizing,

        /** A note being drawn, whose length the same press is still setting.

            Its own gesture rather than a second use of `resizing`, because the
            two want opposite things of a leftward drag: an edge stops at its
            note's start, and a drawn note is a SPAN between the press and the
            pointer, so it grows the other way instead. Sharing the branch is
            why drawing right to left collapsed to a one-division note.
        */
        drawing,

        selecting,
        velocity,
        auditioning,
        erasing,
        painting,
        slicing
    };

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::ValueTree currentPattern() const;

    /** Refills the toolbar's channel list from the project. Called whenever a
        channel is added, removed or renamed - the strip holds names, and a
        stale one is a channel the user cannot find.
    */
    void updateChannelList();

    juce::ValueTree noteAt (juce::Point<int>) const;

    /** Deletes every note the pointer passed over between two drag samples.

        Sampled along the segment rather than at its ends: a drag reports a
        handful of positions per second, so a quick sweep skips whole notes if
        you only look where the pointer happened to be reported.
    */
    void eraseAlong (juce::Point<int> from, juce::Point<int> to);

    int numSteps() const;

    /** Steps in one beat, which is what a span on the ruler snaps to. Not the
        note snap grid: a loop is a musical span, and shift already means
        "select" up here so it cannot also mean "suspend the grid" the way it
        does over the notes.
    */
    int stepsPerBeat() const;

    /** Steps in one bar, which is the unit a pattern's own length is measured
        in - see ProjectEdits::fitPatternToNotes.
    */
    int stepsPerBar() const;

    /** Sets the pattern's length to what its notes need, in whole bars.

        Every gesture that adds, moves, resizes or removes a note ends here, so
        the roll never shows a bar the arrangement will not play and never stops
        short of one it will. Inside the caller's transaction, so the length and
        the edit that changed it undo together.
    */
    void fitPatternLength (juce::UndoManager&);

    /** Steps in one cell of the current snap grid. Always at least one, so
        every snapped expression is the identity at the finest division and the
        gestures behave exactly as they did before there was a grid.
    */
    int snapSteps() const;

    /** Writes a note in the cell under this point, unless one is already there.

        Returns true if it wrote one, so a stroke can tell whether it has done
        anything worth repainting.
    */
    bool paintNoteAt (juce::Point<int> position);

    /** Cuts every note on the channel that the segment from `from` to `to`
        crosses, at the step where it crosses that note's own row.
    */
    void sliceAlong (juce::Point<int> from, juce::Point<int> to);

    // --- geometry ------------------------------------------------------------
    /** The tool strip, and everything below it. Every other area is measured
        from `contentArea()` rather than from the component's own top, so the
        toolbar's height is stated once and cannot leave one part of the layout
        behind when it changes.
    */
    juce::Rectangle<int> toolbarArea() const;
    juce::Rectangle<int> contentArea() const;

    /** The cursor for a point in the roll, from the same hit test a press uses.
        Private: a test drives mouseMove and reads getMouseCursor(), which is
        the path the pointer actually takes. */
    juce::MouseCursor cursorFor (juce::Point<int> position) const;

    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;

    /** The band at the lane's top edge that a drag grabs. */
    juce::Rectangle<int> velocityResizeArea() const;

    float contentWidth() const;
    int stepAtX (int x) const;
    int pitchAtY (int y) const;
    int firstVisiblePitch() const;
    juce::Rectangle<float> boundsForNote (const juce::ValueTree& note) const;

    /** Where a note's velocity bar is drawn in the lane.

        One function for painting and for hit-testing: they disagreed by 8px,
        which is why the bar top was never under the cursor that was supposedly
        dragging it.
    */
    juce::Rectangle<float> velocityBarBounds (const juce::ValueTree& note) const;

    /** The velocity bar under this point, or an invalid tree. */
    juce::ValueTree velocityBarAt (juce::Point<int>) const;

    /** Sounds `pitch` on the selected channel, releasing whatever was sounding.
        A no-op when that pitch and that velocity are already what is sounding.

        `velocity` comes from WHERE ACROSS THE KEY the press landed - see
        auditionVelocityForX. A no-op on pitch alone would swallow a slide along
        one key, which is the whole gesture on a keyboard 54 pixels wide.
    */
    void startAudition (int pitch, float velocity);

    void stopAudition();
    bool isOnRightEdge (const juce::ValueTree& note, juce::Point<int>) const;

    void updateScrollBars();
    void selectOnly (const juce::ValueTree& note);
    bool isSelected (const juce::ValueTree& note) const;
    void deleteSelection();
    void selectAllOnChannel();
    void applyVelocityAt (juce::Point<int>);
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocityLane (juce::Graphics&);
    juce::Colour channelColour() const;

    /** How finely an erase sweep is sampled between two drag positions.

        A fixed distance in pixels, and it stays fixed now that a row is not:
        it is how often the POINTER is looked at, and a sweep across a taller
        row is not a sweep anyone makes more slowly. Small enough that nothing
        can be skipped at the densest row height.
    */
    static constexpr int eraseStridePx = 6;

    static_assert (eraseStridePx < tokens::size::pianoRowMin,
                   "an erase sweep must sample more often than once a row");

    // The document's rule, not the view's - NoteTools owns it, beside the
    // transpose that clamps against it. Aliased rather than re-spelled so the
    // roll's own thirty uses read as they always did.
    static constexpr int lowestPitch = NoteTools::lowestPitch;   ///< C0
    static constexpr int highestPitch = NoteTools::highestPitch; ///< C8
    static constexpr int numRows = highestPitch - lowestPitch + 1;

    /** How far into the lane's top edge a press counts as a resize. The same
        band the playlist's track headers use, for the same reason: thin enough
        that the rest of the lane is still the lane. */
    static constexpr int resizeBandHeight = tokens::space::xs;

    /** Space above and below a velocity bar. Shared by painting and hit-testing
        so the two cannot drift apart again.
    */
    static constexpr int barPadding = 4;

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;

    TimelineView timeline;

    /** Play and stop, eased. The most frequent state change in the
        application, and a hard cut in all four views before this. */
    timelinePaint::PlayheadState playhead { *this };

    /** Scrub, span-select and clear, shared with the playlist and the channel
        rack rather than written here a second time.
    */
    ruler::Gesture rulerGesture;

    juce::ScrollBar horizontalScroll { false };
    juce::ScrollBar verticalScroll { true };
    bool updatingScrollBars = false;

    /** The pitch rows: their height, the scroll down them, and the clamps.
        Not static any more, and not const. See getRowHeight.
    */
    RowView rows { tokens::size::pianoRowDefault, 0.0, tokens::size::pianoRowMin,
                   tokens::size::pianoRowMax };

    /** See getVelocityHeight. Was a static constexpr 62, which is why the lane
        could not be resized while its cursor said it could. */
    int velocityLaneHeight = tokens::size::velocityLaneDefault;

    /** Set while a press on the lane's top edge is dragging it. Screen
        coordinates and a delta, for the reason PlaylistTrackHeader gives: the
        lane's own frame moves under the pointer as the height changes, and
        getDistanceFromDragStart asks the mouse SOURCE, which no synthetic event
        ever pressed. */
    bool resizingVelocityLane = false;
    int velocityResizeOriginY = 0;
    int velocityHeightAtDragStart = tokens::size::velocityLaneDefault;

    juce::Array<juce::ValueTree> selection;
    juce::ValueTree draggedNote;
    Gesture gesture = Gesture::none;
    int dragStepOffset = 0;
    int dragPitchOffset = 0;
    juce::Point<int> dragOrigin;

    // Whether the erase sweep actually removed anything, so a right-click that
    // hit nothing can be told from one that did.
    bool erasedDuringGesture = false;

    // Where the erase sweep last looked. mouseDrag reports discrete positions,
    // so the segment between two of them is what actually has to be swept.
    juce::Point<int> lastErasePosition;
    juce::Rectangle<int> rubberBand;
    juce::Array<juce::ValueTree> selectionAtDragStart;

    // Where each selected note started, so a multi-note move stays rigid rather
    // than every note snapping to the same offset.
    juce::Array<juce::Point<int>> selectionOrigins;

    // What the roll was last pointed at, so a change of channel or pattern can be
    // told from every other thing EditorState broadcasts about.
    int lastSeenChannelId = -1;
    int lastSeenPatternId = -1;

    /** Where the line was last painted, in pixels, and -1 for "nowhere".

        In PIXELS because that is what the eye sees moving: the trigger used to
        be the integer step, so at any useful zoom the line stood still for
        several frames and then jumped a whole cell. The playlist's own timer
        has used half a pixel since it was written, and for the same reason.
    */
    float lastPlayheadX = -1.0f;
    bool lastPlaying = false;
    bool didFitOnce = false;

    /** The key currently sounding under the pointer, so it can be lit and
        released. -1 when nothing is being auditioned.
    */
    int auditionPitch = -1;

    /** What it is sounding AT, so a slide across one key re-triggers it. Held
        beside the pitch because "already sounding" is a question about the
        pair: on a 54px keyboard the whole velocity range is one key wide. */
    float auditionVelocity = 0.0f;

    juce::ValueTree draggedVelocityNote;

    PianoRollToolbar toolbar;

    /** The cell the paint stroke last wrote into, so dragging within one cell
        does not try to write the same note over and over.
    */
    juce::Point<int> lastPaintedCell { -1, -1 };

    /** The slice line while it is being drawn. The cut happens on release: a
        note cut mid-drag would be re-cut into fragments as the pointer moved.
    */
    juce::Point<int> sliceStart, sliceEnd;

    /** Kept between openings of the randomize dialog, so a second pass does not
        start from the defaults again.
    */
    NoteTools::RandomizeOptions randomizeOptions;

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};

} // namespace dew
