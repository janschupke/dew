// =============================================================================
// The piano roll's pointer gestures.
//
// The same class, a second translation unit, for the reason PianoRollPaint.cpp
// gives: these read a dozen members between them and set a dozen more.
//
// One press has nine possible meanings - the keyboard, the ruler, the velocity
// lane, an erase sweep, a slice, a note, a rubber band, a paint stroke and
// adding a note - and which it is depends on the tool, the modifiers and what
// the hit test found. The four handlers stay together so that decision reads
// as one.
//
// Auditioning comes with them: it belongs to the keyboard, which is pressed
// and released by the same handlers. The velocity lane's own arithmetic went
// to PianoRollVelocity.cpp when this file reached the length gate; what is
// left here is which of the nine meanings a press has.
//
// The drag state stays declared on the class. mouseUp resets every field of it
// in one place, and a gesture that ends in a file which cannot see the whole
// set is how one gets left latched.
// =============================================================================

#include "i18n/Strings.h"
#include "ui/PianoRollComponent.h"

#include "ui/PianoRollNotes.h"

#include "model/Ids.h"
#include "model/NoteTools.h"
#include "model/ProjectEdits.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;
using namespace pianoRoll;

void PianoRollComponent::mouseMove (const juce::MouseEvent& event)
{
    setMouseCursor (cursorFor (event.getPosition()));
}

/** One hit test decides both what a press does and what the pointer looks like.

    Notes are not components - the whole roll is painted into this one - so the
    cursor cannot come from a child and has to be derived here. Sharing the test
    is what keeps the two honest: a cursor that says "drag me" where a press
    does something else is worse than no cursor at all.
*/
juce::MouseCursor PianoRollComponent::cursorFor (juce::Point<int> position) const
{
    if (rulerArea().contains (position) || keyboardArea().contains (position))
        return cursor::clickable;

    if (velocityArea().contains (position))
    {
        // The lane's top edge makes it taller; a bar in it changes a number.
        // Two different gestures, and they were one cursor over the whole lane
        // - so the pointer offered a resize everywhere, including where there
        // was nothing to resize and nothing to drag.
        if (velocityResizeArea().contains (position))
            return cursor::resizeY;

        return velocityBarAt (position).isValid() ? cursor::value : cursor::idle;
    }

    if (! noteArea().contains (position))
        return cursor::idle;

    // A tool outranks what is under the pointer: with the eraser or the slice
    // tool a note is not something to pick up, it is something to act on.
    if (getTool() != RollTool::select)
        return cursor::nib;

    const auto note = noteAt (position);

    if (! note.isValid())
        return cursor::idle;

    return isOnRightEdge (note, position) ? cursor::resizeX : cursor::move;
}

void PianoRollComponent::mouseExit (const juce::MouseEvent&)
{
    // Without this the last cursor the roll chose survives the pointer leaving
    // it, and a window edge keeps a resize arrow that means nothing there.
    setMouseCursor (cursor::idle);
}

void PianoRollComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    // On the lane's own edge, a double-click puts it back - the same way a
    // double-click on a playlist track header resets the lane height there.
    if (velocityResizeArea().contains (event.getPosition()))
    {
        setVelocityHeight (size::velocityLaneDefault);
        return;
    }

    // On the ruler, a double-click clears the span - one rule, shared with the
    // playlist and the channel rack rather than repeated in each of them.
    if (rulerArea().contains (event.getPosition()))
    {
        rulerGesture.mouseDoubleClick (event);
        return;
    }

    // The keyboard gutter used to frame the pattern here, which meant a
    // double-click on a piano key both auditioned it twice and threw the view
    // away. Framing lives on the toolbar now, where it can be seen.
    juce::ignoreUnused (event);
}

void PianoRollComponent::eraseAlong (juce::Point<int> from, juce::Point<int> to)
{
    lastErasePosition = to;

    auto pattern = currentPattern();

    if (! pattern.isValid())
        return;

    auto& undo = document.getUndoManager();
    const auto area = noteArea();

    // Step along the segment at a stride finer than the smallest thing that can
    // be hit, so nothing between two drag samples survives.
    const auto distance = from.getDistanceFrom (to);
    const auto steps = juce::jmax (1, (int) std::ceil ((double) distance / eraseStridePx));

    bool erasedAny = false;

    for (int i = 0; i <= steps; ++i)
    {
        const auto t = (float) i / (float) steps;
        const juce::Point<int> point {
            juce::roundToInt ((float) from.x + t * (float) (to.x - from.x)),
            juce::roundToInt ((float) from.y + t * (float) (to.y - from.y))
        };

        if (! area.contains (point))
            continue;

        auto note = noteAt (point);

        if (! note.isValid())
            continue;

        selection.removeAllInstancesOf (note);
        ProjectEdits::removeNote (pattern, note, &undo);
        erasedAny = true;
    }

    if (erasedAny)
    {
        erasedDuringGesture = true;
        repaint();
    }
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    auto pattern = currentPattern();

    if (! pattern.isValid())
        return;

    dragOrigin = event.getPosition();

    // The keyboard used to be inert: mouseDown returned here without ever
    // testing it, so clicking a piano key did nothing at all.
    if (keyboardArea().contains (event.getPosition()))
    {
        gesture = Gesture::auditioning;
        startAudition (pitchAtY (event.y));
        return;
    }

    // The ruler used to be inert: a press there fell through every branch and
    // was dropped by the noteArea guard below. Everything it does now lives in
    // ruler::Gesture, which is why this is one line rather than three branches
    // the playlist also has a copy of.
    if (rulerArea().contains (event.getPosition()))
    {
        rulerGesture.mouseDown (event);
        return;
    }

    if (velocityArea().contains (event.getPosition()))
    {
        // The edge before the lane, so a press aimed at the boundary makes the
        // lane taller rather than writing a velocity of nearly one into
        // whatever note happened to be under it. A popup press starts neither.
        if (! event.mods.isPopupMenu() && velocityResizeArea().contains (event.getPosition()))
        {
            resizingVelocityLane = true;
            velocityResizeOriginY = event.getScreenPosition().y;
            velocityHeightAtDragStart = velocityLaneHeight;
            return;
        }

        if (event.mods.isPopupMenu())
            return;

        gesture = Gesture::velocity;

        // A press that lands ON a bar grabs it, so a vertical drag reshapes
        // that one note wherever the pointer then goes. A press that misses
        // does NOT end the gesture: the bar is three pixels wide at any zoom
        // worth using, so requiring a hit made the whole lane inert, and the
        // drag has to be able to sweep into bars it did not start on - the same
        // rule the erase sweep over the grid already follows.
        draggedVelocityNote = velocityBarAt (event.getPosition());

        document.getUndoManager().beginNewTransaction ("Change velocity");
        applyVelocityAt (event.getPosition());
        return;
    }

    if (! noteArea().contains (event.getPosition()))
        return;

    auto& undo = document.getUndoManager();
    auto note = noteAt (event.getPosition());

    // Right-drag or alt-drag erases, which is the FL convention. It starts a
    // gesture rather than acting once and returning: this used to delete a
    // single note per press and open a transaction for each, so a right-DRAG
    // did nothing at all and clearing a bar was a bar's worth of undo steps.
    // Starting on empty space is deliberate - it is how you sweep INTO notes.
    if (event.mods.isPopupMenu() || event.mods.isAltDown())
    {
        gesture = Gesture::erasing;
        undo.beginNewTransaction ("Erase notes");
        erasedDuringGesture = false;
        lastErasePosition = event.getPosition();
        eraseAlong (event.getPosition(), event.getPosition());
        return;
    }

    // The slice tool takes a plain drag anywhere in the grid, over notes
    // included - it cannot cut a dense passage if landing on a note starts a
    // move instead. Every modifier gesture above still wins, so erasing and
    // rubber-banding mean the same thing here as in any other tool.
    if (toolbar.getTool() == RollTool::slice && ! event.mods.isCommandDown()
        && ! event.mods.isCtrlDown())
    {
        gesture = Gesture::slicing;
        sliceStart = event.getPosition();
        sliceEnd = sliceStart;
        repaint();
        return;
    }

    if (note.isValid())
    {
        if (event.mods.isShiftDown())
        {
            if (isSelected (note))
                selection.removeAllInstancesOf (note);
            else
                selection.add (note);

            repaint();
            return;
        }

        if (! isSelected (note))
            selectOnly (note);

        draggedNote = note;

        if (isOnRightEdge (note, event.getPosition()))
        {
            gesture = Gesture::resizing;
            undo.beginNewTransaction ("Resize note");
        }
        else
        {
            gesture = Gesture::moving;
            dragStepOffset = stepAtX (event.x) - (int) note[ids::step];
            dragPitchOffset = pitchAtY (event.y) - (int) note[ids::pitch];

            selectionOrigins.clearQuick();

            for (const auto& selected : selection)
                selectionOrigins.add ({ (int) selected[ids::step], (int) selected[ids::pitch] });

            undo.beginNewTransaction (selection.size() == 1 ? "Move note" : "Move notes");
        }

        return;
    }

    // Empty space with a modifier: rubber band. Without one: draw a note, and
    // let the same drag set its length, so a note is one gesture.
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        gesture = Gesture::selecting;
        selectionAtDragStart = selection;
        rubberBand = { event.x, event.y, 0, 0 };
        return;
    }

    // The paint tool writes a note per grid cell the pointer crosses, rather
    // than one note whose length the drag then sets. Drawing-and-sizing is the
    // select tool's gesture and is not duplicated here.
    if (toolbar.getTool() == RollTool::paint)
    {
        gesture = Gesture::painting;
        lastPaintedCell = { -1, -1 };
        undo.beginNewTransaction ("Paint notes");

        // A stroke repeats whatever is selected, so shaping one note and then
        // painting writes that note rather than the last one that happened to
        // be dragged. Read before the selection is cleared, and through the
        // same remember/read path a resize uses - one source of note defaults,
        // not two.
        if (! selection.isEmpty())
            editorState.rememberNote ((int) selection.getFirst()[ids::lengthSteps],
                                      (double) selection.getFirst()[ids::velocity]);

        selection.clearQuick();

        if (paintNoteAt (event.getPosition()))
            repaint();

        return;
    }

    undo.beginNewTransaction ("Add note");

    const auto snap = snapSteps();

    draggedNote = ProjectEdits::addNote (
        pattern, editorState.getSelectedChannelId(), NoteTools::snapFloor (stepAtX (event.x), snap),
        juce::jmax (snap, NoteTools::snapCeil (editorState.getLastNoteLengthSteps(), snap)),
        pitchAtY (event.y), (float) editorState.getLastNoteVelocity(), &undo);
    selectOnly (draggedNote);
    ProjectEdits::growPatternToFitNotes (pattern, &undo);

    gesture = Gesture::drawing;
    repaint();
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& event)
{
    auto& undo = document.getUndoManager();

    if (resizingVelocityLane)
    {
        // SCREEN coordinates and a delta from the press, not a position: the
        // lane's own top edge is what this moves, so its local frame slides
        // under the pointer while the pointer is being read in it. Up is a
        // taller lane, which is why the delta is subtracted.
        setVelocityHeight (velocityHeightAtDragStart
                           - (event.getScreenPosition().y - velocityResizeOriginY));
        return;
    }

    if (gesture == Gesture::auditioning)
    {
        // Sliding down the keyboard plays what it passes over.
        startAudition (pitchAtY (event.y));
        return;
    }

    if (gesture == Gesture::velocity)
    {
        applyVelocityAt (event.getPosition());
        return;
    }

    if (gesture == Gesture::erasing)
    {
        eraseAlong (lastErasePosition, event.getPosition());
        return;
    }

    if (rulerGesture.mouseDrag (event))
        return;

    if (gesture == Gesture::painting)
    {
        if (paintNoteAt (event.getPosition()))
            repaint();

        return;
    }

    if (gesture == Gesture::slicing)
    {
        sliceEnd = event.getPosition();
        repaint();
        return;
    }

    if (gesture == Gesture::selecting)
    {
        rubberBand = juce::Rectangle<int>::leftTopRightBottom (
            juce::jmin (dragOrigin.x, event.x), juce::jmin (dragOrigin.y, event.y),
            juce::jmax (dragOrigin.x, event.x), juce::jmax (dragOrigin.y, event.y));

        selection = selectionAtDragStart;

        const auto channelId = editorState.getSelectedChannelId();

        for (const auto& note : currentPattern())
            if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
                && boundsForNote (note).toNearestInt().intersects (rubberBand)
                && ! selection.contains (note))
                selection.add (note);

        repaint();
        return;
    }

    if (! draggedNote.isValid())
        return;

    // Shift suspends the grid for the length of a drag, which is the only way
    // to reach an off-grid position without going back to the dropdown.
    const auto snap = event.mods.isShiftDown() ? 1 : snapSteps();

    if (gesture == Gesture::drawing)
    {
        // A SPAN between the cell the press landed in and the cell the pointer
        // is in, so the note grows whichever way the hand goes. It used to
        // share the resize branch below, which reads the note's own start - a
        // start the press had already fixed - so dragging left produced a
        // negative length, the jmax clamped it to one step, and the note the
        // release then remembered as the default was that one step.
        //
        // dragOrigin rather than the note's step: the note MOVES during this
        // gesture, and an anchor read from the thing being moved is not an
        // anchor. The rubber band a few lines up normalises the same way.
        const auto anchor = NoteTools::snapFloor (stepAtX (dragOrigin.x), snap);
        const auto here = stepAtX (event.x);

        const auto start = juce::jmax (0, juce::jmin (anchor, NoteTools::snapFloor (here, snap)));
        const auto end = juce::jmax (anchor + snap, NoteTools::snapCeil (here + 1, snap));

        ProjectEdits::moveNote (draggedNote, start, (int) draggedNote[ids::pitch], &undo);
        ProjectEdits::resizeNote (draggedNote, end - start, &undo);
    }
    else if (gesture == Gesture::resizing)
    {
        // The note's END lands on a grid line, rather than its length becoming
        // a multiple of the grid: a note that already starts off-grid should be
        // draggable to a beat, not merely to a beat's width.
        //
        // One DIVISION is the floor, not one step: a drawn note is never
        // shorter than a cell of the grid it was drawn on, and an edge dragged
        // past its own start should land on the same length.
        const auto start = (int) draggedNote[ids::step];
        const auto end = NoteTools::snapCeil (stepAtX (event.x) + 1, snap);

        ProjectEdits::resizeNote (draggedNote, juce::jmax (snap, end - start), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        // The grabbed note's absolute target snaps, and the delta that produces
        // is applied to the whole selection. Snapping the delta would leave the
        // note you are holding permanently off the grid; snapping each note
        // separately would collapse a chord's internal offsets onto one step.
        const auto targetStep = juce::jmax (
            0, NoteTools::snapNearest (stepAtX (event.x) - dragStepOffset, snap));
        const auto targetPitch = pitchAtY (event.y) - dragPitchOffset;

        const auto deltaStep = targetStep - (int) draggedNote[ids::step];
        const auto deltaPitch = targetPitch - (int) draggedNote[ids::pitch];

        if (deltaStep == 0 && deltaPitch == 0)
            return;

        // Move every selected note by the same delta, clamped as a group, so a
        // chord keeps its shape when it hits step 0 or the top of the keyboard.
        int allowedStep = deltaStep;
        int allowedPitch = deltaPitch;

        for (const auto& note : selection)
        {
            allowedStep = juce::jmax (allowedStep, -(int) note[ids::step]);
            allowedPitch = juce::jlimit (lowestPitch - (int) note[ids::pitch],
                                         highestPitch - (int) note[ids::pitch], allowedPitch);
        }

        for (auto note : selection)
            ProjectEdits::moveNote (note, (int) note[ids::step] + allowedStep,
                                    (int) note[ids::pitch] + allowedPitch, &undo);
    }

    ProjectEdits::growPatternToFitNotes (currentPattern(), &undo);
    repaint();
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& event)
{
    stopAudition();
    draggedVelocityNote = {};
    resizingVelocityLane = false;

    // Falls through rather than returning: a ruler gesture leaves none of the
    // note-editing state set, so the reset at the bottom is a no-op for it.
    rulerGesture.mouseUp (event);

    // A right-press that erased nothing is not an erase - it is a click on empty
    // space, and that means "deselect". A right-DRAG still erases, and a press
    // that swept even one note away is an erase however short it was.
    if (gesture == Gesture::erasing && ! erasedDuringGesture
        && ! event.mouseWasDraggedSinceMouseDown())
    {
        selection.clearQuick();
    }

    // The cut happens here rather than during the drag: a note already cut
    // would be cut again into fragments every time the pointer wobbled, and a
    // press that turns out to cross nothing should not open a transaction.
    if (gesture == Gesture::slicing)
    {
        sliceEnd = event.getPosition();
        sliceAlong (sliceStart, sliceEnd);
    }

    // The next note drawn takes the shape of the last one, so writing a passage
    // of held or quiet notes does not mean re-editing every one.
    if (draggedNote.isValid()
        && (gesture == Gesture::drawing || gesture == Gesture::resizing
            || gesture == Gesture::moving))
        editorState.rememberNote ((int) draggedNote[ids::lengthSteps],
                                  (double) draggedNote[ids::velocity]);

    draggedNote = {};
    gesture = Gesture::none;
    rubberBand = {};
    sliceStart = sliceEnd = {};
    lastPaintedCell = { -1, -1 };
    selectionAtDragStart.clearQuick();
    repaint();
}

void PianoRollComponent::startAudition (int pitch)
{
    const auto wanted = juce::jlimit (lowestPitch, highestPitch, pitch);

    if (wanted == auditionPitch)
        return;

    stopAudition();

    const auto channelIndex = ProjectEdits::channelIndexForId (document.getState(),
                                                               editorState.getSelectedChannelId());

    if (channelIndex < 0)
        return;

    auditionPitch = wanted;
    engine.previewNoteOn (channelIndex, wanted, (float) editorState.getLastNoteVelocity());
    repaint (keyboardArea());
}

void PianoRollComponent::stopAudition()
{
    if (auditionPitch < 0)
        return;

    // Released everywhere rather than on one channel: the selected channel can
    // change mid-drag, and a note left ringing is worse than an extra message.
    engine.previewAllOff();
    auditionPitch = -1;
    repaint (keyboardArea());
}

} // namespace dew
