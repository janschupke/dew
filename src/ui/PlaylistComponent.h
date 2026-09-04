#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "ui/ConfirmPanel.h"
#include "ui/EditorState.h"
#include "model/AutomationTargets.h"
#include "ui/PlaylistToolbar.h"
#include "ui/PlaylistTrackHeader.h"
#include "ui/TimelineRuler.h"
#include "ui/TimelinePaint.h"
#include "ui/AutomationLane.h"
#include "ui/TimelineView.h"
#include "ui/RowView.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

class SamplePool;

/** The arrangement: pattern clips placed on tracks along a bar timeline.

    Click empty space to place the current pattern, drag a clip to move it -
    including onto another track - drag its right edge to lengthen it (which
    repeats the pattern, as FL does), right-click or alt-click to remove it, and
    double-click it to open its pattern in the piano roll.

    The timeline is in bars rather than steps, but it is the same TimelineView
    the note editors use, so scrolling and zooming behave identically.
*/
class PlaylistComponent : public juce::Component,
                          private juce::Timer,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    /** The sample pool is passed in rather than reached through the engine.

        A file cache is not the engine's to lend out - the engine only needs
        somewhere to get samples from, which is what SampleProvider is for. The
        UI wants the peaks too, so it takes the pool itself, the way
        InstrumentPanel and SampleSection already do.
    */
    PlaylistComponent (ProjectDocument&, AudioEngine&, EditorState&, SamplePool* = nullptr);
    ~PlaylistComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** What removing a track asks first. See ConfirmHook. */
    ConfirmHook confirmDestructive;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    void refresh();

    /** Creates an automation for a curated target and places a clip for it, so
        choosing a target produces something visible rather than an entry in a
        list nobody can see.
    */
    juce::ValueTree createAutomationClip (const AutomationTarget&, int startBar, int lengthBars);

    /** Called when a clip is double-clicked, after its pattern has been made
        current. The playlist does not know about tabs; whoever owns them does.
    */
    std::function<void()> onOpenPatternInPianoRoll;

    // --- geometry, so a test can aim at a clip instead of recomputing layout --
    juce::Rectangle<float> getBoundsForClip (const juce::ValueTree& clip, int trackIndex) const
    {
        return boundsForClip (clip, trackIndex);
    }

    const TimelineView& getTimeline() const noexcept
    {
        return timeline;
    }
    int getNumTracks() const;

    /** The ruler strip, in this component's coordinates, so a test can aim at
        it rather than recomputing the layout and drifting from it - the piano
        roll already publishes its areas for the same reason.
    */
    juce::Rectangle<int> getRulerArea() const
    {
        return { tokens::size::gutterTrack, rulerTop(), (int) contentWidth(),
                 tokens::size::rulerHeight };
    }

    /** The tool strip, so a test can drive the tools and the zoom buttons
        through the same seam the user reaches them by.
    */
    PlaylistToolbar& getToolbar() noexcept
    {
        return toolbar;
    }

    PlaylistTool getTool() const noexcept
    {
        return toolbar.getTool();
    }
    void setTool (PlaylistTool tool)
    {
        toolbar.setTool (tool);
    }

    /** Frames the whole song: all of it visible, scrolled to the start. */
    void zoomToFit();

    void addTrack();
    void removeTrack (juce::ValueTree track);

    /** Drives a track header's context-menu item without opening the menu.

        juce::PopupMenu::showMenuAsync cannot run headlessly, so the menu is the
        only part of this a test cannot reach; everything it does is behind here.
        Returns false if there is no such track.
    */
    bool applyTrackMenuChoice (int trackIndex, int choice);

    /** The items a track header's menu offers, for a test to assert against. */
    juce::StringArray trackMenuItems (int trackIndex) const;

    /** The same, for the menu a clip's right-click opens.

        A bar has no y, and which segment of a curve you are on IS a y - so the
        POSITION pair is the real seam and the bar pair is written on top of it,
        aiming at the middle of the row. Two builders would be two chances to
        disagree about what is under the pointer.
    */
    juce::StringArray clipMenuItemsAt (juce::Point<int>) const;
    bool applyClipMenuChoiceAt (juce::Point<int>, int choice);

    juce::StringArray clipMenuItems (int trackIndex, int bar) const;
    bool applyClipMenuChoice (int trackIndex, int bar, int choice);

    /** Where an automation point sits, for tests and for hit-testing. */
    juce::Point<float> pointPosition (const juce::ValueTree& clip, int trackIndex,
                                      const juce::ValueTree& point) const;

    /** Where a clip's curve is drawn, so a test can aim at a segment rather than
        recomputing the layout and drifting from it - the same reason
        getBoundsForClip and getRulerArea are public.
    */
    automationLane::Geometry laneGeometryFor (const juce::ValueTree& clip, int trackIndex) const
    {
        return laneGeometry (clip, trackIndex);
    }

    // --- the height of a lane -------------------------------------------------
    /** The height of EVERY lane.

        One number for the whole view, not one per track. A per-track height
        sounds more flexible and is not: an automation curve is only editable at
        a height somebody chose for it, and a view where that is true of some
        lanes and not others is a view where the same gesture works or does not
        depending on where you aim it.

        On the component rather than in the document, because it is not part of
        the music and must not make a project dirty or land on the undo stack;
        and on the component rather than on EditorState, because that is a
        ChangeBroadcaster whose four other listeners have no interest in it. The
        precedent is PianoRollComponent's own pitchScrollPx and TimelineView's
        pixelsPerStep: view geometry lives on the view.
    */
    int getTrackHeight() const noexcept
    {
        return rows.height;
    }

    /** How far the lanes are scrolled, in pixels. Public so a test can ask how
        far one wheel notch travelled without recomputing laneY and drifting
        from it. */
    double getTrackScrollPx() const noexcept
    {
        return rows.scrollPx;
    }

    /** The one mutator. Clamps to the ladder, keeps the lane under the middle of
        the view where it is, and re-lays everything that depends on the height.
    */
    void setTrackHeight (int height);

    /** A factor to multiply the height by, or 0 to fit the tracks to the window
        - the same shape zoomBy/zoomToFit already report on the other axis, so
        the toolbar's two groups mean one thing twice.
    */
    void zoomTracksBy (double factor);
    void fitTracksToWindow();

    // --- the resize grip -----------------------------------------------------
    /** A drag on a track header's bottom edge, which sets the height of EVERY
        lane - there is one height, and the edge you grabbed is just the one
        the pointer is nearest.

        Three calls rather than one because the middle of a drag is a different
        state from either end: the scroll offset is held still between the
        first and the last, so the grabbed edge stays under the hand instead of
        being re-centred out from under it.

        `deltaY` is measured from where the press landed, in SCREEN pixels - the
        header's own frame is what the drag is moving, so a delta read in it
        would be measured against a ruler that is changing length.
    */
    void beginRowHeightDrag();
    void dragRowHeightBy (int laneIndex, int deltaY);
    void endRowHeightDrag();

    /** The strip the lanes are drawn in, so a test can aim at a lane rather than
        recomputing the layout and drifting from it - the same reason
        getRulerArea exists.
    */
    juce::Rectangle<int> getLaneArea() const;

    /** Scrolls the lanes to this offset in pixels, clamped.

        The seam a test reaches the vertical scroll by, because a ScrollBar
        cannot be dragged headlessly - and the one the wheel and the bar itself
        both go through, so there is one clamp rather than three.
    */
    void scrollTracksTo (double offsetPx);

private:
    // Scrubbing and range-selecting are not here: the ruler's whole gesture
    // lives in ruler::Gesture, shared with the piano roll and the channel rack.
    enum class Gesture
    {
        none,
        moving,
        resizing,
        draggingPoint,
        bendingSegment,
        painting
    };

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::ValueTree playlist() const;
    int numBars() const;
    int barAtX (int x) const;
    int trackAtY (int y) const;
    juce::ValueTree trackAt (int index) const;

    /** The removal itself, once it has been agreed to. By index, because a
        playlist track carries no id and a ValueTree must not be held across an
        async dialog. */
    void removeTrackNow (int index);
    juce::Rectangle<float> boundsForClip (const juce::ValueTree& clip, int trackIndex) const;
    bool isOnRightEdge (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    int tracksBottom() const;
    float contentWidth() const;

    /** The top of the ruler, and the top of the lanes below it. Every other
        vertical measurement is taken from these rather than from the
        component's own top, so the toolbar's height is stated once and cannot
        leave one part of the layout behind when it changes.

        Still constexpr: neither depends on how tall a lane is.
    */
    static constexpr int rulerTop()
    {
        return tokens::size::stripToolbar;
    }
    static constexpr int lanesTop()
    {
        return tokens::size::stripToolbar + tokens::size::rulerHeight;
    }

    /** The top of lane `index`, with the vertical scroll already applied.

        EVERY vertical measurement goes through here: the headers' bounds, a
        clip's bounds, hit testing and paint. Before there was a scroll offset,
        header and lane were impossible to desynchronise because both read
        `lanesTop() + i * rowHeight`; this is what keeps that true now that there
        is one, and it is the reason the offset is not a Viewport.
    */
    float laneY (int index) const noexcept
    {
        return (float) lanesTop() + rows.yForRow (index);
    }

    /** The bottom of the strip lanes may be drawn in: the last lane's bottom, or
        the bottom of the view when the tracks overflow it. Bar lines, the
        selection band, the beyond-end wash and the playhead all stop here.
    */
    int lanesBottom() const;

    /** Above the horizontal scrollbar's strip, which is reserved whether or not
        the bar is showing - the add-track button already assumed this.
    */
    int viewBottom() const noexcept
    {
        return getHeight() - tokens::size::scrollThickness;
    }
    int laneViewHeight() const noexcept
    {
        return juce::jmax (0, viewBottom() - lanesTop());
    }

    /** Lays a clip of the current pattern in the cell under this point, unless
        one is already there. Returns true if it wrote one, so a stroke can tell
        whether it has done anything worth repainting.
    */
    bool paintClipAt (juce::Point<int> position);

    /** Copies a clip rather than moving it: a mod-drag leaves the original
        where it was, and a mod-shift-drag gives the copy a pattern of its own.
    */
    void beginCopyDrag (int targetTrackIndex, int targetBar);
    float playheadX() const;
    /** Re-clamps the scroll and re-ranges the scrollbar. This used to also
        recompute the zoom from the song's length, on every resize - which is
        why the playlist could not be zoomed at all: any change re-fitted it.
    */
    void updateScrollBar();
    void zoomBy (double factor, float anchorX);
    void rebuildHeaders();
    void openPatternOf (const juce::ValueTree& clip);
    void showAutomationMenu();

    /** The menu a right-click on a lane opens, and what its items do.

        What is offered depends on what is under the pointer: a curve point, a
        clip, or empty space. Built and applied separately so a test can drive
        the actions without a menu that cannot open headlessly.
    */
    juce::PopupMenu buildClipMenu (const juce::ValueTree& track, int bar) const;
    void applyClipChoice (juce::ValueTree track, int bar, int choice);

    /** Set while a clip menu is open, so its items act on the point that was
        actually under the pointer rather than re-hit-testing from a bar.
    */
    mutable juce::ValueTree menuPoint;

    /** And the segment, when the pointer is on the curve but not on a point.
        Mutable for the same reason menuPoint is: the seam a test reads is const,
        and it has to latch what it is about to describe. */
    mutable juce::ValueTree menuSegment;

    /** Latches what is under a position, for both the press and the seam. */
    void latchMenuContext (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    juce::ValueTree automationOf (const juce::ValueTree& clip) const;

    /** Where this clip's curve is drawn, and what its horizontal axis means. */
    automationLane::Geometry laneGeometry (const juce::ValueTree& clip, int trackIndex) const;

    /** A point, a segment, or nothing. The ONE hit test: the press, the cursor
        and the menu all go through it, or the cursor promises something the
        press will not do.
    */
    automationLane::Hit laneHit (const juce::ValueTree& clip, int trackIndex,
                                 juce::Point<int>) const;

    /** The automation point under this position, within grabbing distance. */
    juce::ValueTree pointAt (const juce::ValueTree& clip, int trackIndex, juce::Point<int>) const;

    /** Turns a position inside an automation clip into a step and a 0..1 value. */
    void positionToCurve (const juce::ValueTree& clip, int trackIndex, juce::Point<int>,
                          double& step, double& value) const;

    /** An audio clip: its channel's waveform, in its channel's colour. */
    void paintAudioClip (juce::Graphics&, const juce::ValueTree& clip,
                         juce::Rectangle<float> bounds, bool audible);

    void paintAutomationClip (juce::Graphics&, const juce::ValueTree& clip, int trackIndex,
                              juce::Rectangle<float> bounds, bool audible);

    /** The lanes and the clips on them. Split out of paint() because it is the
        only part of it that is CLIPPED - a lane can be scrolled now, and a
        half-scrolled first track would otherwise draw its stripe and its divider
        over the ruler. Returns how many tracks it walked, which is what tells
        paint() whether to draw the empty state.
    */
    int paintLanes (juce::Graphics&, int bottom, bool anySolo);

    ProjectDocument& document;
    AudioEngine& engine;
    EditorState& editorState;
    SamplePool* samplePool = nullptr;

    PlaylistToolbar toolbar;

    TimelineView timeline;

    /** Play and stop, eased. The most frequent state change in the
        application, and a hard cut in all four views before this. */
    timelinePaint::PlayheadState playhead { *this };

    /** Scrub, span-select and clear. This timeline counts BARS, so the gesture
        works in bars here and in steps everywhere else - it never converts, so
        it cannot get the conversion wrong.
    */
    ruler::Gesture rulerGesture;

    juce::ScrollBar horizontalScroll { false };

    /** Vertical scroll, because a lane can now be taller than a sixth of the
        window and four of them stop fitting.

        A bare scrollbar and an offset, the way PianoRollComponent does it, and
        NOT a juce::Viewport. The playlist paints its lanes rather than having
        them as children, so a Viewport would scroll the headers and leave the
        arrangement behind; and the toolbar, the ruler and the horizontal bar all
        have to stay pinned. The channel rack can use one because its rows really
        are children.
    */
    juce::ScrollBar verticalScroll { true };

    /** The lanes: their height, the scroll down them, and the clamps. Shared
        with the piano roll's pitch rows rather than written here a second
        time - see RowView.
    */
    RowView rows { tokens::size::trackHeightDefault, 0.0, tokens::size::trackHeightMin,
                   tokens::size::trackHeightMax };

    /** Latched for the length of a resize drag. See beginRowHeightDrag. */
    bool resizingRows = false;
    int heightAtDragStart = tokens::size::trackHeightDefault;
    double heightDragScrollPx = 0.0;

    bool updatingScrollBar = false;

    /** Holds the headers and the add-track row. NOT a Viewport and not a
        scroller: it is here only to CLIP.

        JUCE clips a child to its parent, and a header scrolled half off the top
        would otherwise be drawn over the ruler. It carries no scroll position of
        its own - rows.scrollPx stays the single source of truth, which is what
        keeps the headers in step with lanes that are painted rather than laid
        out.
    */
    juce::Component headerHolder;

    juce::OwnedArray<PlaylistTrackHeader> headers;
    DewButton addAutomationButton { "+ Automation", DewButton::Role::ghost };

    // In the header column below the last track: the next empty row of the list,
    // where the track it adds will appear.
    DewButton addTrackButton { "+ Track", DewButton::Role::ghost };

    juce::ValueTree draggedClip;
    juce::ValueTree draggedClipTrack;
    juce::ValueTree draggedPoint;

    /** The bend gesture's own origin and starting value, latched at the press.

        Its own, because Component::getDistanceFromDragStart is always zero in a
        headless harness - and ABSOLUTE from where the drag began, which is what
        makes the gesture path-independent: out and back returns to exactly where
        it started rather than to wherever the accumulated deltas landed.
    */
    juce::Point<int> bendOrigin;
    double bendAtDragStart = 0.0;

    /** Which segment the pointer is over, so only a change of it repaints. */
    int hoveredSegment = -1;
    juce::ValueTree hoveredSegmentClip;
    Gesture gesture = Gesture::none;
    int dragBarOffset = 0;
    int dropTrackIndex = -1;

    /** A mod-drag copies rather than moves, and mod-shift gives the copy its
        own pattern. Latched at the press and cleared by the copy itself, so a
        drag makes ONE copy however far it then travels.
    */
    bool dragCopies = false;
    bool dragCopyIsUnique = false;

    /** The cell the paint stroke last wrote into, so dragging within one cell
        does not try to lay the same clip over and over.
    */
    juce::Point<int> lastPaintedCell { -1, -1 };

    /** Whether the view belongs to the user yet.

        Until it does, a layout re-fits the song to the window - the playlist
        should open showing the whole arrangement, and the FIRST layout is no
        good for deciding that because it can happen before the window has its
        real size. From the moment someone zooms or scrolls, the view is theirs
        and a layout leaves it alone. Re-fitting on every layout, which is what
        this did, is why the playlist could not be zoomed at all.
    */
    bool viewIsUsers = false;

    // The playhead is repainted on its own strip at 60Hz; repainting the whole
    // arrangement that often is what made the line jump a bar at a time.
    float lastPaintedPlayheadX = -1.0f;
    bool lastPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};

} // namespace dew
