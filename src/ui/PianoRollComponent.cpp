#include "PianoRollComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"
#include "design/Tokens.h"
#include "primitives/DewControls.h"

namespace dew
{

namespace
{

bool isBlackKey (int pitch)
{
    switch (((pitch % 12) + 12) % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

juce::String noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1);
}

} // namespace

PianoRollComponent::PianoRollComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("pianoRoll");
    setWantsKeyboardFocus (true);

    document.getState().addListener (this);
    editorState.addChangeListener (this);

    horizontalScroll.addListener (this);
    verticalScroll.addListener (this);
    addAndMakeVisible (horizontalScroll);
    addAndMakeVisible (verticalScroll);

    // Middle C somewhere near the middle, rather than at the very top where the
    // default scroll position would leave it.
    pitchScrollPx = (double) ((highestPitch - 72) * rowHeight);

    startTimerHz (tokens::motion::playheadHz);
}

PianoRollComponent::~PianoRollComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void PianoRollComponent::refresh()
{
    document.getState().addListener (this);
    selection.clearQuick();
    didFitOnce = false;
    updateScrollBars();
    repaint();
}

// --- lookup ------------------------------------------------------------------

juce::ValueTree PianoRollComponent::currentPattern() const
{
    return ProjectEdits::findPattern (document.getState(), editorState.getCurrentPatternId());
}

int PianoRollComponent::numSteps() const
{
    const auto pattern = currentPattern();
    return pattern.isValid() ? juce::jmax (1, (int) pattern[ids::lengthSteps]) : 16;
}

juce::Colour PianoRollComponent::channelColour() const
{
    const auto channel = ProjectEdits::findChannel (document.getState(),
                                                    editorState.getSelectedChannelId());

    if (! channel.isValid())
        return tokens::colour::accent;

    return juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6));
}

// --- geometry ----------------------------------------------------------------

juce::Rectangle<int> PianoRollComponent::rulerArea() const
{
    return { keyboardWidth, 0, juce::jmax (0, getWidth() - keyboardWidth - scrollThickness), rulerHeight };
}

juce::Rectangle<int> PianoRollComponent::noteArea() const
{
    const auto top = rulerHeight;
    const auto bottom = juce::jmax (top, getHeight() - scrollThickness - velocityHeight);

    return { keyboardWidth, top,
             juce::jmax (0, getWidth() - keyboardWidth - scrollThickness), bottom - top };
}

juce::Rectangle<int> PianoRollComponent::keyboardArea() const
{
    const auto notes = noteArea();
    return { 0, notes.getY(), keyboardWidth, notes.getHeight() };
}

juce::Rectangle<int> PianoRollComponent::velocityArea() const
{
    const auto top = juce::jmax (0, getHeight() - scrollThickness - velocityHeight);

    return { keyboardWidth, top,
             juce::jmax (0, getWidth() - keyboardWidth - scrollThickness), velocityHeight };
}

float PianoRollComponent::contentWidth() const
{
    return (float) noteArea().getWidth();
}

int PianoRollComponent::stepAtX (int x) const
{
    // Deliberately not clamped to the pattern length. Writing past the end is
    // how a pattern gets longer - the region beyond is painted as inert so it
    // is clearly outside the pattern, but it is still writable, and doing so
    // grows the pattern to fit. Clamping here made growPatternToFitNotes
    // unreachable from the mouse.
    return juce::jmax (0, timeline.stepAtX ((float) (x - keyboardWidth)));
}

int PianoRollComponent::firstVisiblePitch() const
{
    return highestPitch - (int) (pitchScrollPx / rowHeight);
}

int PianoRollComponent::pitchAtY (int y) const
{
    const auto rowsDown = (int) std::floor (((double) (y - noteArea().getY()) + pitchScrollPx) / rowHeight);
    return juce::jlimit (lowestPitch, highestPitch, highestPitch - rowsDown);
}

juce::Rectangle<float> PianoRollComponent::boundsForNote (const juce::ValueTree& note) const
{
    const auto step   = (int) note[ids::step];
    const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);
    const auto pitch  = (int) note[ids::pitch];

    const auto notes = noteArea();
    const auto x = (float) keyboardWidth + timeline.xForStep ((double) step);
    const auto y = (float) notes.getY() + (float) ((highestPitch - pitch) * rowHeight) - (float) pitchScrollPx;

    return { x, y, (float) (length * timeline.pixelsPerStep), (float) rowHeight };
}

bool PianoRollComponent::isOnRightEdge (const juce::ValueTree& note, juce::Point<int> position) const
{
    const auto bounds = boundsForNote (note);
    const auto edge = juce::jmin (8.0f, bounds.getWidth() * 0.35f);

    return (float) position.x >= bounds.getRight() - edge;
}

juce::ValueTree PianoRollComponent::noteAt (juce::Point<int> position) const
{
    const auto pattern = currentPattern();
    const auto channelId = editorState.getSelectedChannelId();

    // Backwards, so the note painted on top is the one you grab.
    for (int i = pattern.getNumChildren(); --i >= 0;)
    {
        const auto note = pattern.getChild (i);

        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && boundsForNote (note).contains (position.toFloat()))
            return note;
    }

    return {};
}

// --- selection ---------------------------------------------------------------

bool PianoRollComponent::isSelected (const juce::ValueTree& note) const
{
    return selection.contains (note);
}

void PianoRollComponent::selectOnly (const juce::ValueTree& note)
{
    selection.clearQuick();

    if (note.isValid())
        selection.add (note);
}

void PianoRollComponent::selectAllOnChannel()
{
    selection.clearQuick();

    const auto channelId = editorState.getSelectedChannelId();

    for (const auto& note : currentPattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
            selection.add (note);

    repaint();
}

void PianoRollComponent::deleteSelection()
{
    if (selection.isEmpty())
        return;

    auto pattern = currentPattern();
    auto& undo = document.getUndoManager();

    undo.beginNewTransaction (selection.size() == 1 ? "Delete note"
                                                    : "Delete " + juce::String (selection.size()) + " notes");

    // Removing a note calls back into valueTreeChildRemoved, which drops it from
    // `selection` - iterating the live array skipped every other note and left
    // most of a multi-note selection behind.
    const auto doomed = selection;
    selection.clearQuick();

    for (const auto& note : doomed)
        ProjectEdits::removeNote (pattern, note, &undo);

    repaint();
}

// --- scrolling ---------------------------------------------------------------

void PianoRollComponent::updateScrollBars()
{
    const juce::ScopedValueSetter<bool> quiet (updatingScrollBars, true);

    const auto notes = noteArea();

    timeline.clampScroll (contentWidth(), numSteps());

    horizontalScroll.setRangeLimits (0.0, (double) numSteps(), juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps,
                                      timeline.visibleSteps (contentWidth()),
                                      juce::dontSendNotification);

    const auto contentHeight = (double) (numRows * rowHeight);
    pitchScrollPx = juce::jlimit (0.0, juce::jmax (0.0, contentHeight - notes.getHeight()), pitchScrollPx);

    verticalScroll.setRangeLimits (0.0, contentHeight, juce::dontSendNotification);
    verticalScroll.setCurrentRange (pitchScrollPx, (double) notes.getHeight(), juce::dontSendNotification);
}

void PianoRollComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBars)
        return;

    if (bar == &horizontalScroll)
        timeline.scrollOffsetSteps = start;
    else
        pitchScrollPx = start;

    repaint();
}

void PianoRollComponent::centreOnPitch (int pitch)
{
    const auto rowTop = (double) ((highestPitch - juce::jlimit (lowestPitch, highestPitch, pitch)) * rowHeight);
    pitchScrollPx = rowTop - noteArea().getHeight() * 0.5 + rowHeight * 0.5;
    updateScrollBars();
}

void PianoRollComponent::scrollToNotesIfOffscreen()
{
    const auto area = noteArea();

    if (area.getHeight() <= 0)
        return;

    const auto channelId = editorState.getSelectedChannelId();

    int lowest = highestPitch + 1;
    int highest = lowestPitch - 1;
    bool anyVisible = false;

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto pitch = (int) note[ids::pitch];
        lowest = juce::jmin (lowest, pitch);
        highest = juce::jmax (highest, pitch);

        if (area.toFloat().intersects (boundsForNote (note)))
            anyVisible = true;
    }

    if (anyVisible)
        return;

    if (highest >= lowest)
    {
        centreOnPitch ((lowest + highest) / 2);
        return;
    }

    // No notes yet: the channel's own pitch is where writing will start.
    const auto channel = ProjectEdits::findChannel (document.getState(), channelId);
    centreOnPitch (channel.isValid() ? (int) channel[ids::basePitch] : 72);
}

void PianoRollComponent::captureView (double& zoom, double& scroll, double& pitchScroll) const
{
    zoom = timeline.pixelsPerStep;
    scroll = timeline.scrollOffsetSteps;
    pitchScroll = pitchScrollPx;
}

void PianoRollComponent::applyView (double zoom, double scroll, double pitchScroll)
{
    timeline.pixelsPerStep = juce::jlimit (TimelineView::minPixelsPerStep,
                                           TimelineView::maxPixelsPerStep, zoom);
    timeline.scrollOffsetSteps = juce::jmax (0.0, scroll);
    pitchScrollPx = juce::jmax (0.0, pitchScroll);

    // A restored view is the user's, not something to reframe over.
    didFitOnce = true;

    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomToFit()
{
    timeline.fit (numSteps(), contentWidth());
    updateScrollBars();
    repaint();
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& event,
                                         const juce::MouseWheelDetails& wheel)
{
    // Natural scrolling flips the sign of the deltas, and JUCE reports that
    // rather than applying it. Ignoring it - which this did - means the roll
    // scrolls the wrong way for anyone with the system default on.
    const auto direction = wheel.isReversed ? -1.0 : 1.0;
    const auto deltaX = (double) wheel.deltaX * direction;
    const auto deltaY = (double) wheel.deltaY * direction;

    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        // Zoom around the pointer. deltaY is small; exaggerate it or a zoom
        // takes a dozen notches to be noticeable.
        timeline.zoomAround (std::pow (2.0, deltaY * 3.0), (float) (event.x - keyboardWidth));
    }
    else if (event.mods.isShiftDown())
    {
        timeline.scrollOffsetSteps -= (deltaX + deltaY) * 8.0;
    }
    else
    {
        pitchScrollPx -= deltaY * 3.0 * rowHeight;
        timeline.scrollOffsetSteps -= deltaX * 8.0;
    }

    updateScrollBars();
    repaint();
}

void PianoRollComponent::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    // Trackpad pinch. The factor is already multiplicative, so it goes straight
    // through - and anchoring on the pointer is what stops the music walking
    // out from under the fingers doing the pinching.
    if (scaleFactor <= 0.0f)
        return;

    timeline.zoomAround ((double) scaleFactor, (float) (event.x - keyboardWidth));
    updateScrollBars();
    repaint();
}

bool PianoRollComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (key.getTextCharacter() == 'a' && (key.getModifiers().isCommandDown()))
    {
        selectAllOnChannel();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        selection.clearQuick();
        repaint();
        return true;
    }

    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
    {
        timeline.zoomAround (1.5, contentWidth() * 0.5f);
        updateScrollBars();
        repaint();
        return true;
    }

    if (key.getTextCharacter() == '-' || key.getTextCharacter() == '_')
    {
        timeline.zoomAround (1.0 / 1.5, contentWidth() * 0.5f);
        updateScrollBars();
        repaint();
        return true;
    }

    return false;
}

// --- mouse -------------------------------------------------------------------

void PianoRollComponent::mouseMove (const juce::MouseEvent& event)
{
    if (! noteArea().contains (event.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    const auto note = noteAt (event.getPosition());

    setMouseCursor (note.isValid() && isOnRightEdge (note, event.getPosition())
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Double-clicking the keyboard gutter or the ruler frames the pattern, which
    // is the quickest way back after zooming into a detail.
    if (keyboardArea().contains (event.getPosition()) || rulerArea().contains (event.getPosition()))
        zoomToFit();
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

    if (velocityArea().contains (event.getPosition()))
    {
        gesture = Gesture::velocity;

        // Grab a bar rather than snapping wherever the click landed, and do not
        // open an undo transaction for a click on empty lane space.
        draggedVelocityNote = velocityBarAt (event.getPosition());

        if (! draggedVelocityNote.isValid())
        {
            gesture = Gesture::none;
            return;
        }

        document.getUndoManager().beginNewTransaction ("Change velocity");
        applyVelocityAt (event.getPosition());
        return;
    }

    if (! noteArea().contains (event.getPosition()))
        return;

    auto& undo = document.getUndoManager();
    auto note = noteAt (event.getPosition());

    // Right-click or alt-click deletes, which is the FL convention.
    if (event.mods.isPopupMenu() || event.mods.isAltDown())
    {
        if (note.isValid())
        {
            undo.beginNewTransaction ("Delete note");
            ProjectEdits::removeNote (pattern, note, &undo);
            selection.removeAllInstancesOf (note);
            repaint();
        }
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

    undo.beginNewTransaction ("Add note");

    draggedNote = ProjectEdits::addNote (pattern, editorState.getSelectedChannelId(),
                                         stepAtX (event.x),
                                         editorState.getLastNoteLengthSteps(),
                                         pitchAtY (event.y),
                                         (float) editorState.getLastNoteVelocity(),
                                         &undo);
    selectOnly (draggedNote);
    ProjectEdits::growPatternToFitNotes (pattern, &undo);

    gesture = Gesture::resizing;
    repaint();
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& event)
{
    auto& undo = document.getUndoManager();

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

    if (gesture == Gesture::resizing)
    {
        const auto length = stepAtX (event.x) - (int) draggedNote[ids::step] + 1;
        ProjectEdits::resizeNote (draggedNote, juce::jmax (1, length), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        const auto targetStep  = juce::jmax (0, stepAtX (event.x) - dragStepOffset);
        const auto targetPitch = pitchAtY (event.y) - dragPitchOffset;

        const auto deltaStep  = targetStep - (int) draggedNote[ids::step];
        const auto deltaPitch = targetPitch - (int) draggedNote[ids::pitch];

        if (deltaStep == 0 && deltaPitch == 0)
            return;

        // Move every selected note by the same delta, clamped as a group, so a
        // chord keeps its shape when it hits step 0 or the top of the keyboard.
        int allowedStep = deltaStep;
        int allowedPitch = deltaPitch;

        for (const auto& note : selection)
        {
            allowedStep  = juce::jmax (allowedStep, -(int) note[ids::step]);
            allowedPitch = juce::jlimit (lowestPitch - (int) note[ids::pitch],
                                         highestPitch - (int) note[ids::pitch], allowedPitch);
        }

        for (auto note : selection)
            ProjectEdits::moveNote (note,
                                    (int) note[ids::step] + allowedStep,
                                    (int) note[ids::pitch] + allowedPitch, &undo);
    }

    ProjectEdits::growPatternToFitNotes (currentPattern(), &undo);
    repaint();
}

void PianoRollComponent::mouseUp (const juce::MouseEvent&)
{
    stopAudition();
    draggedVelocityNote = {};

    // The next note drawn takes the shape of the last one, so writing a passage
    // of held or quiet notes does not mean re-editing every one.
    if (draggedNote.isValid() && (gesture == Gesture::resizing || gesture == Gesture::moving))
        editorState.rememberNote ((int) draggedNote[ids::lengthSteps],
                                  (double) draggedNote[ids::velocity]);

    draggedNote = {};
    gesture = Gesture::none;
    rubberBand = {};
    selectionAtDragStart.clearQuick();
    repaint();
}

void PianoRollComponent::startAudition (int pitch)
{
    const auto wanted = juce::jlimit (lowestPitch, highestPitch, pitch);

    if (wanted == auditionPitch)
        return;

    stopAudition();

    // The engine indexes channels by position, and so does the snapshot; the
    // editor knows the channel by id, so resolve it the same way the snapshot
    // builder does rather than assuming they agree.
    int index = 0;
    int channelIndex = -1;

    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if ((int) channel[ids::id] == editorState.getSelectedChannelId())
            channelIndex = index;

        ++index;
    }

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

juce::Rectangle<float> PianoRollComponent::velocityBarBounds (const juce::ValueTree& note) const
{
    const auto area = velocityArea();
    const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
    const auto barWidth = (float) juce::jlimit (3.0, 14.0, timeline.pixelsPerStep * 0.7);
    const auto x = (float) keyboardWidth + timeline.xForStep ((double) (int) note[ids::step]);

    const auto floor = (float) area.getBottom() - (float) barPadding;
    const auto height = velocity * (float) juce::jmax (1, area.getHeight() - barPadding * 2);

    return { x + 1.0f, floor - height, barWidth, height };
}

juce::ValueTree PianoRollComponent::velocityBarAt (juce::Point<int> position) const
{
    const auto channelId = editorState.getSelectedChannelId();

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        // Generous vertically: the bar is a few pixels wide and its top is what
        // you aim at, so the whole column counts as a grab.
        const auto bar = velocityBarBounds (note);
        const auto column = juce::Rectangle<float> (bar.getX() - 2.0f, (float) velocityArea().getY(),
                                                    bar.getWidth() + 4.0f, (float) velocityArea().getHeight());

        if (column.contains (position.toFloat()))
            return note;
    }

    return {};
}

void PianoRollComponent::applyVelocityAt (juce::Point<int> position)
{
    const auto lane = velocityArea();

    // Mapped against the same geometry velocityBarBounds draws with. These
    // disagreed by 8px, so the bar top never sat under the cursor dragging it.
    const auto floor = lane.getBottom() - barPadding;
    const auto span = juce::jmax (1, lane.getHeight() - barPadding * 2);
    const auto value = juce::jlimit (0.0, 1.0, (double) (floor - position.y) / (double) span);

    auto& undo = document.getUndoManager();

    if (draggedVelocityNote.isValid())
    {
        ProjectEdits::setNoteVelocity (draggedVelocityNote, value, &undo);
        repaint (lane);
        return;
    }

    const auto channelId = editorState.getSelectedChannelId();
    const auto step = stepAtX (position.x);

    // Only notes that start under the pointer, so dragging across the lane
    // paints a velocity curve without also hitting every held note under it.
    for (auto note : currentPattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId
            && (int) note[ids::step] == step
            && (selection.isEmpty() || isSelected (note)))
            ProjectEdits::setNoteVelocity (note, value, &undo);

    repaint (lane);
}

// --- notifications -----------------------------------------------------------

void PianoRollComponent::timerCallback()
{
    const auto step = (int) engine.getPlayheadSteps();

    if (step != lastPlayheadStep)
    {
        lastPlayheadStep = step;

        if (engine.isPlaying() && engine.getMode() == Transport::Mode::pattern)
        {
            const auto wrapped = (double) (step % juce::jmax (1, numSteps()));
            const auto before = timeline.scrollOffsetSteps;
            timeline.ensureVisible (wrapped, contentWidth());

            if (! juce::exactlyEqual (before, timeline.scrollOffsetSteps))
                updateScrollBars();
        }

        repaint();
    }
}

void PianoRollComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    // The pattern length bounds how far the view can scroll, so a change to it
    // has to reach the scrollbars, not only the paint.
    if (property == ids::lengthSteps)
        updateScrollBars();

    repaint();
}
void PianoRollComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)             { repaint(); }

void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    // A selection holding removed notes would delete or move nothing on the next
    // gesture, and looks like the editor ignoring input.
    selection.removeAllInstancesOf (child);
    repaint();
}

void PianoRollComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    selection.clearQuick();
    scrollToNotesIfOffscreen();
    repaint();
}

void PianoRollComponent::resized()
{
    horizontalScroll.setBounds (keyboardWidth, getHeight() - scrollThickness,
                                juce::jmax (0, getWidth() - keyboardWidth - scrollThickness),
                                scrollThickness);

    const auto notes = noteArea();
    verticalScroll.setBounds (getWidth() - scrollThickness, notes.getY(), scrollThickness, notes.getHeight());

    // The first layout frames the pattern; after that the user's zoom is theirs.
    if (! didFitOnce && contentWidth() > 0.0f)
    {
        didFitOnce = true;
        timeline.fit (numSteps(), contentWidth());
        updateScrollBars();
        scrollToNotesIfOffscreen();
        return;
    }

    updateScrollBars();
}

// --- painting ----------------------------------------------------------------

void PianoRollComponent::paintKeyboard (juce::Graphics& g)
{
    using namespace tokens;

    const auto keys = keyboardArea();

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (keys);

    g.setColour (colour::wellDeep);
    g.fillRect (keys);

    const auto firstRow = juce::jmax (0, (int) (pitchScrollPx / rowHeight));
    const auto lastRow  = juce::jmin (numRows - 1, (int) ((pitchScrollPx + keys.getHeight()) / rowHeight));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = highestPitch - row;
        const auto y = (float) keys.getY() + (float) (row * rowHeight) - (float) pitchScrollPx;
        const auto black = isBlackKey (pitch);

        auto colourValue = black ? juce::Colour (0xff1c1f24) : juce::Colour (0xffd8dce3);

        // The key under the pointer lights while it sounds, so a click on the
        // keyboard is visibly doing something and not only audibly.
        if (pitch == auditionPitch)
            colourValue = colour::accent;

        g.setColour (colourValue);
        g.fillRect ((float) keys.getX(), y, (float) (keys.getWidth() - 1), (float) (rowHeight - 1));

        if (pitch % 12 == 0)
        {
            g.setColour (pitch == auditionPitch ? colour::textOnAccent : colour::textOnAccent);
            g.setFont (type::font (type::caption));
            g.drawText (noteName (pitch), keys.getX() + 3, (int) y, keys.getWidth() - 6, rowHeight,
                        juce::Justification::centredLeft, false);
        }
    }

    g.setColour (colour::dividerStrong);
    g.drawVerticalLine (keys.getRight() - 1, (float) keys.getY(), (float) keys.getBottom());
}

void PianoRollComponent::paintRuler (juce::Graphics& g)
{
    using namespace tokens;

    const auto area = rulerArea();
    g.setColour (colour::surface);
    g.fillRect (area);

    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto stepsPerBar = stepsPerBeat * 4;

    // Unclamped, so the ruler does not stop numbering half way across the
    // window. Bars past the end of the pattern are dimmed rather than absent.
    const auto steps = numSteps();
    const auto range = timeline.visibleStepRange (contentWidth());

    g.setFont (type::font (type::caption));

    for (int step = range.getStart(); step <= range.getEnd(); ++step)
    {
        const auto x = (float) keyboardWidth + timeline.xForStep ((double) step);

        if (x > (float) area.getRight())
            break;

        const auto beyond = step >= steps;
        const auto fade = [beyond] (juce::Colour c) { return beyond ? c.withAlpha (0.35f) : c; };

        if (step % stepsPerBar == 0)
        {
            g.setColour (fade (colour::dividerStrong));
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());

            // Bar numbers, but only when there is room for them to be readable.
            if (timeline.pixelsPerStep * stepsPerBar >= 28.0)
            {
                g.setColour (beyond ? colour::textDisabled : colour::textSecondary);
                g.drawText (juce::String (step / stepsPerBar + 1),
                            juce::Rectangle<int> ((int) x + 3, area.getY(), 40, area.getHeight()),
                            juce::Justification::centredLeft, false);
            }
        }
        else if (step % stepsPerBeat == 0 && timeline.pixelsPerStep * stepsPerBeat >= 10.0)
        {
            g.setColour (fade (colour::divider));
            g.drawVerticalLine ((int) x, (float) area.getBottom() - 6.0f, (float) area.getBottom());
        }
    }

    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
}

void PianoRollComponent::paintNotes (juce::Graphics& g)
{
    using namespace tokens;

    const auto area = noteArea();

    g.setColour (colour::wellDeep);
    g.fillRect (area);

    // Clipped to the note area, not to the full width. This used to start at
    // x = 0, which included the keyboard gutter, so any note scrolled past the
    // left edge was painted straight over the keys.
    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto stepsPerBar = stepsPerBeat * 4;

    // --- rows ----------------------------------------------------------------
    const auto firstRow = juce::jmax (0, (int) (pitchScrollPx / rowHeight));
    const auto lastRow  = juce::jmin (numRows - 1, (int) ((pitchScrollPx + area.getHeight()) / rowHeight));

    // Where the last pitch row ends. Only below the note area on a window tall
    // enough to show all 97 rows at once, but if it ever is, that strip should
    // be marked out rather than left as bare background.
    const auto rowsBottom = (float) area.getY() + (float) (numRows * rowHeight) - (float) pitchScrollPx;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = highestPitch - row;
        const auto y = (float) area.getY() + (float) (row * rowHeight) - (float) pitchScrollPx;

        if (isBlackKey (pitch))
        {
            g.setColour (colour::well);
            g.fillRect ((float) area.getX(), y, (float) area.getWidth(), (float) rowHeight);
        }

        g.setColour (pitch % 12 == 0 ? colour::dividerStrong : colour::divider.withAlpha (0.4f));
        g.drawHorizontalLine ((int) y, (float) area.getX(), (float) area.getRight());
    }

    // --- columns -------------------------------------------------------------
    // The UNCLAMPED range: bar lines carry on to the edge of the window even
    // where the pattern has ended, so the grid never stops mid-view.
    const auto steps = numSteps();
    const auto range = timeline.visibleStepRange (contentWidth());

    for (int step = range.getStart(); step <= range.getEnd(); ++step)
    {
        const auto x = (float) keyboardWidth + timeline.xForStep ((double) step);

        if (x > (float) area.getRight())
            break;

        if (step % stepsPerBar == 0)
            g.setColour (colour::dividerStrong);
        else if (step % stepsPerBeat == 0)
            g.setColour (colour::divider);
        else if (timeline.pixelsPerStep >= 6.0)
            g.setColour (colour::divider.withAlpha (0.4f));
        else
            continue;

        g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
    }

    // Past the end of the pattern is still drawn - it is just dimmed, with the
    // end itself marked. It used to be hatched over, which turned every window
    // wider than the music into a dead rectangle.
    const auto endX = (float) keyboardWidth + timeline.xForStep ((double) steps);

    if (endX < (float) area.getRight())
        paint::beyondEnd (g, juce::Rectangle<float> (endX, (float) area.getY(),
                                                     (float) area.getRight() - endX,
                                                     (float) area.getHeight()).toNearestInt(), endX);

    // Below the lowest pitch, for the same reason and in the same idiom.
    if (rowsBottom < (float) area.getBottom())
        paint::inertArea (g, juce::Rectangle<float> ((float) area.getX(), rowsBottom,
                                                     (float) area.getWidth(),
                                                     (float) area.getBottom() - rowsBottom).toNearestInt());

    // --- notes ---------------------------------------------------------------
    const auto pattern = currentPattern();
    const auto channelId = editorState.getSelectedChannelId();
    const auto colourForChannel = channelColour();

    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE))
            continue;

        const auto bounds = boundsForNote (note).reduced (0.5f, 1.0f);

        if (! bounds.intersects (area.toFloat()))
            continue;

        if ((int) note[ids::ch] != channelId)
        {
            // Other channels' notes are context, not something editable here.
            g.setColour (colour::dividerStrong.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds, 2.0f);
            continue;
        }

        // Velocity is visible on the note itself, not only in the lane, so a
        // quiet note reads as quiet while you are writing the melody.
        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);

        g.setColour (colourForChannel.withAlpha (0.35f + 0.6f * velocity));
        g.fillRoundedRectangle (bounds, 2.0f);

        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel.brighter (0.4f));
        g.drawRoundedRectangle (bounds, 2.0f, isSelected (note) ? 1.6f : 1.0f);
    }

    // --- playhead ------------------------------------------------------------
    if (engine.isPlaying() && engine.getMode() == Transport::Mode::pattern)
    {
        const auto step = (double) ((int) engine.getPlayheadSteps() % steps);
        const auto x = (float) keyboardWidth + timeline.xForStep (step);

        g.setColour (colour::playhead.withAlpha (0.2f));
        g.fillRect (x, (float) area.getY(), (float) timeline.pixelsPerStep, (float) area.getHeight());
        g.setColour (colour::playhead);
        g.fillRect (x, (float) area.getY(), 1.5f, (float) area.getHeight());
    }

    // --- rubber band ---------------------------------------------------------
    if (gesture == Gesture::selecting && ! rubberBand.isEmpty())
    {
        g.setColour (colour::accent.withAlpha (0.18f));
        g.fillRect (rubberBand);
        g.setColour (colour::accent);
        g.drawRect (rubberBand, 1);
    }
}

void PianoRollComponent::paintVelocityLane (juce::Graphics& g)
{
    using namespace tokens;

    const auto area = velocityArea();

    g.setColour (colour::well);
    g.fillRect (area);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (area.getY(), 0.0f, (float) getWidth());

    // Label gutter, so the lane is identifiable rather than a mystery strip.
    paint::sectionHeading (g, { 0, area.getY(), keyboardWidth, area.getHeight() }, "VEL",
                           juce::Justification::centred);

    const auto channelId = editorState.getSelectedChannelId();
    const auto colourForChannel = channelColour();
    const auto barWidth = (float) juce::jlimit (2.0, 14.0, timeline.pixelsPerStep * 0.7);

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    // Bar lines, so a velocity bar can be read against the same grid as the
    // note it belongs to. The lane had no grid at all, which made it hard to
    // tell which bar went with which note once the view was zoomed out.
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto stepsPerBar = stepsPerBeat * 4;
    const auto steps = numSteps();

    const auto laneRange = timeline.visibleStepRange (contentWidth());

    for (int step = laneRange.getStart(); step <= laneRange.getEnd(); ++step)
    {
        if (step % stepsPerBar != 0)
            continue;

        const auto lineX = (float) keyboardWidth + timeline.xForStep ((double) step);

        if (lineX > (float) area.getRight())
            break;

        g.setColour (step >= steps ? colour::dividerStrong.withAlpha (0.35f) : colour::dividerStrong);
        g.drawVerticalLine ((int) lineX, (float) area.getY(), (float) area.getBottom());
    }

    const auto laneEndX = (float) keyboardWidth + timeline.xForStep ((double) steps);

    if (laneEndX < (float) area.getRight())
        paint::beyondEnd (g, juce::Rectangle<float> (laneEndX, (float) area.getY(),
                                                     (float) area.getRight() - laneEndX,
                                                     (float) area.getHeight()).toNearestInt(), laneEndX);

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
        const auto x = (float) keyboardWidth + timeline.xForStep ((double) (int) note[ids::step]);

        if (x < (float) area.getX() - barWidth || x > (float) area.getRight())
            continue;

        const auto height = velocity * (float) (area.getHeight() - 8);
        const auto bar = juce::Rectangle<float> (x + 1.0f, (float) area.getBottom() - 4.0f - height,
                                                 barWidth, height);

        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel.withAlpha (0.85f));
        g.fillRect (bar);
        g.setColour (colour::wellDeep);
        g.fillEllipse (bar.getX() - 1.0f, bar.getY() - 2.0f, barWidth + 2.0f, 4.0f);
        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel);
        g.fillEllipse (bar.getX(), bar.getY() - 1.5f, barWidth, 3.0f);
    }
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    using namespace tokens;

    g.fillAll (colour::background);

    paintNotes (g);
    paintKeyboard (g);
    paintRuler (g);
    paintVelocityLane (g);

    // Corner above the keyboard, where the ruler and the gutter meet.
    g.setColour (colour::surface);
    g.fillRect (0, 0, keyboardWidth, rulerHeight);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) keyboardWidth);

    if (! ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId()).isValid())
    {
        paint::emptyState (g, noteArea(), "Select a channel in the Channel Rack");
    }
}

} // namespace dew
