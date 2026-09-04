#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "model/NoteTools.h"
#include "app/ProjectDocument.h"
#include "ui/EditorState.h"
#include "ui/PianoRollToolbar.h"
#include "ui/TimelineRuler.h"
#include "ui/TimelinePaint.h"
#include "ui/TimelineView.h"
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
        return rowHeight;
    }
    void setRowHeight (int);

    /** Multiplies the row height, or fits every pitch that has a note in it
        when `factor` is 0 - the shape ZoomButtons reports. */
    void zoomRowsBy (double factor);

    void fitRowsToWindow();

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
    void openRandomizeDialog();

private:
    // Scrubbing and range-selecting are not here: the ruler's whole gesture
    // lives in ruler::Gesture, which the playlist and the channel rack share.
    enum class Gesture
    {
        none,
        moving,
        resizing,
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
        A no-op when that pitch is already the one being auditioned.
    */
    void startAudition (int pitch);
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

    static constexpr int lowestPitch = 12;   ///< C0
    static constexpr int highestPitch = 108; ///< C8
    static constexpr int numRows = highestPitch - lowestPitch + 1;
    static constexpr int velocityHeight = 62;

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

    double pitchScrollPx = 0.0;

    /** Not static any more, and not const. See getRowHeight. */
    int rowHeight = tokens::size::pianoRowDefault;

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

    int lastPlayheadStep = -1;
    bool lastPlaying = false;
    bool didFitOnce = false;

    /** The key currently sounding under the pointer, so it can be lit and
        released. -1 when nothing is being auditioned.
    */
    int auditionPitch = -1;

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
