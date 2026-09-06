// =============================================================================
// PlaylistComponent's pointer gestures.
//
// The same class, a second translation unit, for the reason PlaylistPaint.cpp
// gives: these read a dozen members between them and set a dozen more.
//
// One press has nine possible meanings here - the ruler, alt-delete, the popup
// menu, an automation point, a curve segment, moving a clip, resizing one,
// painting one, and adding one - and which it is depends on the tool, the
// modifiers and what the hit test found. Keeping the four handlers in one file
// is what lets that be read as a single decision rather than four.
//
// The drag state itself stays declared on the class: mouseUp resets every
// field of it in one place, and a gesture that ends in a file that cannot see
// the whole set is how one gets left latched.
// =============================================================================

#include "ui/MenuSeam.h"
#include "ui/PlaylistComponent.h"

#include "model/AutomationCurve.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/AutomationLane.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

void PlaylistComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    // On the ruler, a double-click clears the span - one rule, shared with the
    // piano roll and the channel rack rather than repeated in each of them.
    if (event.x >= size::gutterTrack && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDoubleClick (event);
        return;
    }

    if (event.x < size::gutterTrack || event.y < lanesTop())
        return;

    const auto track = trackAt (trackAtY (event.y));

    if (! track.isValid())
        return;

    const auto clip = ProjectEdits::findClipAtBar (track, barAtX (event.x));

    if (auto automation = automationOf (clip); automation.isValid())
    {
        // Double-click inside a curve adds a point there. A single click has to
        // stay "move the clip", or an automation clip could not be moved.
        double step = 0.0, value = 0.0;
        positionToCurve (clip, trackAtY (event.y), event.getPosition(), step, value);

        auto& undo = document.getUndoManager();
        undo.beginNewTransaction ("Add automation point");
        ProjectEdits::addAutomationPoint (automation, step, value, &undo);
        repaint();
        return;
    }

    // A pattern clip is a reference to a pattern, so the obvious thing to do
    // with one is to go and edit that pattern. Previously the only route was
    // the pattern selector in the transport bar.
    openPatternOf (clip);
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& event)
{
    // It asks for focus in its constructor but never took it, so its keys only
    // worked if something else had happened to hand it over.
    grabKeyboardFocus();

    // The ruler used to be excluded outright by this guard, so the playlist's
    // was as inert as the piano roll's. Everything it does now lives in
    // ruler::Gesture, which is why this is one line rather than three branches
    // the piano roll also had a copy of.
    if (event.x >= size::gutterTrack && event.y >= rulerTop() && event.y < lanesTop())
    {
        rulerGesture.mouseDown (event);
        return;
    }

    if (event.x < size::gutterTrack || event.y < lanesTop() || event.y >= lanesBottom())
        return;

    const auto trackIndex = trackAtY (event.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return;

    const auto bar = barAtX (event.x);
    auto clip = ProjectEdits::findClipAtBar (track, bar);
    auto& undo = document.getUndoManager();

    // Alt-click still removes outright: it is the sweep-to-clear gesture the
    // piano roll and step grid share, and losing it would cost a habit to gain a
    // menu that is already on the other button.
    if (event.mods.isAltDown())
    {
        // On a point, remove the point; anywhere else, remove the clip. Without
        // this a curve could gain points but never lose one.
        if (auto point = pointAt (clip, trackIndex, event.getPosition()); point.isValid())
        {
            undo.beginNewTransaction ("Remove automation point");
            ProjectEdits::removeAutomationPoint (automationOf (clip), point, &undo);
            repaint();
            return;
        }

        if (clip.isValid())
        {
            undo.beginNewTransaction ("Delete clip");
            ProjectEdits::removeClip (track, clip, &undo);
            repaint();
        }
        return;
    }

    // Right-click opens a menu instead, so deleting a clip is a thing you choose
    // rather than a thing that happens on the way past.
    if (event.mods.isPopupMenu())
    {
        latchMenuContext (clip, trackIndex, event.getPosition());

        auto menu = buildClipMenu (track, bar);
        showMenuAt<PlaylistComponent> (menu, *this, event,
                                       [track, bar] (PlaylistComponent& playlist, int choice)
                                       { playlist.applyClipChoice (track, bar, choice); });
        return;
    }

    // A point, then the curve, then the clip.
    //
    // The point is first because it is the smaller, more precise target and it
    // sits inside the band its own segments occupy. The curve is before the clip
    // because a press within a few pixels of it is a press ON it - and only
    // within a few pixels, so a press anywhere else in the clip still moves the
    // clip and no gesture is taken away.
    const auto hit = laneHit (clip, trackIndex, event.getPosition());

    if (hit.kind == automationLane::Hit::Kind::point)
    {
        draggedClip = clip;
        draggedClipTrack = track;
        draggedPoint = hit.point;
        dropTrackIndex = trackIndex;
        gesture = Gesture::draggingPoint;
        undo.beginNewTransaction ("Move automation point");
        return;
    }

    if (hit.kind == automationLane::Hit::Kind::segment && automationLane::isBendable (hit.point))
    {
        draggedClip = clip;
        draggedClipTrack = track;
        draggedPoint = hit.point; // the segment's LEFT point owns its bend
        dropTrackIndex = trackIndex;
        gesture = Gesture::bendingSegment;
        bendOrigin = event.getPosition();
        bendAtDragStart = (double) hit.point[ids::curve];
        undo.beginNewTransaction ("Bend automation curve");
        return;
    }

    if (clip.isValid())
    {
        draggedClip = clip;
        draggedClipTrack = track;
        dropTrackIndex = trackIndex;

        if (isOnRightEdge (clip, trackIndex, event.getPosition()))
        {
            gesture = Gesture::resizing;
            undo.beginNewTransaction ("Resize clip");
        }
        else
        {
            // Mod copies rather than moves, and mod-shift gives the copy a
            // pattern of its own. Latched here rather than read on every drag
            // sample: a modifier let go halfway through a drag must not turn a
            // copy back into a move, and the copy has to be made exactly once.
            dragCopies = event.mods.isCommandDown() || event.mods.isCtrlDown();
            dragCopyIsUnique = dragCopies && event.mods.isShiftDown();

            gesture = Gesture::moving;
            dragBarOffset = bar - (int) clip[ids::startBar];
            undo.beginNewTransaction (dragCopies ? "Copy clip" : "Move clip");
        }

        return;
    }

    // The paint tool lays a clip per cell the pointer crosses. Placing one and
    // sizing it with the same drag is the select tool's gesture and is not
    // duplicated here.
    if (toolbar.getTool() == EditorTool::paint)
    {
        gesture = Gesture::painting;
        lastPaintedCell = { -1, -1 };
        undo.beginNewTransaction ("Paint clips");

        if (paintClipAt (event.getPosition()))
            repaint();

        return;
    }

    undo.beginNewTransaction ("Add clip");
    draggedClip = ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar, 1, &undo);
    draggedClipTrack = track;
    dropTrackIndex = trackIndex;
    drawAnchorBar = bar;
    gesture = Gesture::drawing;
    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    updateScrollBar();
    repaint();
}

bool PlaylistComponent::paintClipAt (juce::Point<int> position)
{
    if (position.x < size::gutterTrack || position.y < lanesTop() || position.y >= lanesBottom())
        return false;

    const auto trackIndex = trackAtY (position.y);
    auto track = trackAt (trackIndex);

    if (! track.isValid())
        return false;

    const auto bar = barAtX (position.x);

    if (bar == lastPaintedCell.x && trackIndex == lastPaintedCell.y)
        return false;

    lastPaintedCell = { bar, trackIndex };

    // Anything already occupying this bar, whether it starts here or runs
    // through it - painting over a clip should not stack a second one inside it.
    if (ProjectEdits::findClipAtBar (track, bar).isValid())
        return false;

    auto& undo = document.getUndoManager();

    ProjectEdits::addClip (track, editorState.getCurrentPatternId(), bar,
                           editorState.getLastClipLengthBars(), &undo);
    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    return true;
}

void PlaylistComponent::beginCopyDrag (int targetTrackIndex, int targetBar)
{
    auto targetTrack = trackAt (targetTrackIndex);

    if (! targetTrack.isValid() || ! draggedClip.isValid())
        return;

    auto& undo = document.getUndoManager();
    auto source = draggedClip;

    // A unique copy gets a pattern of its own, so editing it afterwards does
    // not edit every clip that shared the original.
    if (dragCopyIsUnique && ProjectEdits::isMidiClip (source))
    {
        auto pattern = ProjectEdits::findPattern (document.getState(),
                                                  (int) source[ids::patternId]);

        if (pattern.isValid())
        {
            auto fresh = ProjectEdits::duplicatePattern (document.getState(), pattern, &undo);

            if (fresh.isValid())
            {
                source = source.createCopy();
                source.setProperty (ids::patternId, (int) fresh[ids::id], nullptr);
            }
        }
    }

    // The gesture rebinds to the COPY and leaves the original where it was -
    // the same rebinding a cross-track move already forces, for the same reason:
    // the rest of the drag would otherwise edit a node nothing is looking at.
    if (auto copy = ProjectEdits::copyClip (targetTrack, source, targetBar, &undo); copy.isValid())
    {
        draggedClip = copy;
        draggedClipTrack = targetTrack;
        dropTrackIndex = targetTrackIndex;
    }

    dragCopies = false;
    dragCopyIsUnique = false;
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (rulerGesture.mouseDrag (event))
        return;

    if (gesture == Gesture::painting)
    {
        if (paintClipAt (event.getPosition()))
        {
            updateScrollBar();
            repaint();
        }

        return;
    }

    if (! draggedClip.isValid())
        return;

    auto& undo = document.getUndoManager();

    if (gesture == Gesture::bendingSegment)
    {
        // Absolute from where the drag began, not accumulated: that is what
        // makes the gesture path-independent, so dragging out and back returns
        // the bend it started with.
        //
        // Shift is FINER here, and suspends the snap in the point drag below.
        // The two look like a contradiction and are not: this drag changes a
        // VALUE and that one changes a POSITION, which is the line Gestures.h
        // already draws.
        const auto travel = (double) (bendOrigin.y - event.y);
        const auto scale = gesture::isFine (event.mods) ? gesture::fineMultiplier : 1.0;

        auto bend = bendAtDragStart
                    + travel / (double) gesture::dragPixelsForFullRange * scale * 2.0;

        // Up bulges the curve UP whichever way the segment runs. Without this the
        // same hand movement bulges up on a rising segment and down on a falling
        // one.
        const auto points = ProjectEdits::sortedAutomationPoints (automationOf (draggedClip));
        const auto index = points.indexOf (draggedPoint);

        if (index >= 0 && index + 1 < points.size()
            && (double) points[index + 1][ids::value] < (double) draggedPoint[ids::value])
            bend = -bend;

        ProjectEdits::setPointCurve (draggedPoint, bend, &undo);
        repaint();
        return;
    }

    if (gesture == Gesture::draggingPoint)
    {
        double step = 0.0, value = 0.0;
        positionToCurve (draggedClip, dropTrackIndex, event.getPosition(), step, value);

        // Snapped to the BEAT, and shift suspends it - the piano roll's rule. A
        // bar is far too coarse for a curve on a four-bar clip, which is what
        // the playlist's own timeline counts in.
        if (! gesture::isFine (event.mods))
        {
            const auto beat = (double) juce::jmax (1, Meter::of (document.getState()).stepsPerBeat);
            step = std::round (step / beat) * beat;
        }

        ProjectEdits::moveAutomationPoint (automationOf (draggedClip), draggedPoint, step, value,
                                           &undo);
        repaint();
        return;
    }

    if (gesture == Gesture::drawing)
    {
        // A SPAN between the bar the press landed in and the bar under the
        // pointer, so a clip grows whichever way the hand goes. It shared the
        // resize branch below, which measures from the clip's own startBar -
        // fixed by the press - so a leftward drag gave a negative length and
        // the jmax pinned it to one bar.
        const auto here = barAtX (event.x);
        const auto start = juce::jmax (0, juce::jmin (drawAnchorBar, here));
        const auto end = juce::jmax (drawAnchorBar, here) + 1;

        ProjectEdits::moveClip (draggedClip, start, &undo);
        ProjectEdits::resizeClip (draggedClip, end - start, &undo);
    }
    else if (gesture == Gesture::resizing)
    {
        const auto length = barAtX (event.x) - (int) draggedClip[ids::startBar] + 1;
        ProjectEdits::resizeClip (draggedClip, juce::jmax (1, length), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        const auto targetBar = juce::jmax (0, barAtX (event.x) - dragBarOffset);
        const auto targetTrackIndex = juce::jlimit (0, juce::jmax (0, getNumTracks() - 1),
                                                    trackAtY (event.y));

        // On the first drag that actually goes somewhere, so a mod-press that
        // never moves does not litter a copy on top of its own original.
        if (dragCopies
            && (targetBar != (int) draggedClip[ids::startBar]
                || targetTrackIndex != dropTrackIndex))
            beginCopyDrag (targetTrackIndex, targetBar);

        auto targetTrack = trackAt (targetTrackIndex);

        if (targetTrack.isValid() && targetTrack != draggedClipTrack)
        {
            // Crossing tracks replaces the clip's tree, so the drag has to keep
            // following the new one or the rest of the gesture edits a detached
            // node and nothing appears to happen.
            draggedClip = ProjectEdits::moveClipToTrack (draggedClipTrack, draggedClip, targetTrack,
                                                         targetBar, &undo);
            draggedClipTrack = targetTrack;
            dropTrackIndex = targetTrackIndex;
        }
        else
        {
            ProjectEdits::moveClip (draggedClip, targetBar, &undo);
        }
    }

    ProjectEdits::growSongToFitClips (document.getState(), &undo);
    updateScrollBar();
    repaint();
}

void PlaylistComponent::mouseUp (const juce::MouseEvent& event)
{
    // Falls through rather than returning: a ruler gesture leaves none of the
    // clip-dragging state set, so the reset below is a no-op for it.
    rulerGesture.mouseUp (event);

    // The next clip painted takes the length of the last one sized, so laying a
    // run of four-bar clips does not mean resizing every one.
    if (draggedClip.isValid()
        && (gesture == Gesture::drawing || gesture == Gesture::resizing
            || gesture == Gesture::moving))
        editorState.rememberClip ((int) draggedClip[ids::lengthBars]);

    draggedClip = {};
    draggedClipTrack = {};
    draggedPoint = {};
    dropTrackIndex = -1;
    gesture = Gesture::none;
    dragCopies = false;
    dragCopyIsUnique = false;
    lastPaintedCell = { -1, -1 };
    repaint();
}

// --- notifications -----------------------------------------------------------

} // namespace dew
