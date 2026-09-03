#include "ui/PianoRollComponent.h"

#include "model/ChannelColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/DewLookAndFeel.h"
#include "ui/RandomizePanel.h"
#include "ui/TimelineRuler.h"
#include "ui/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

namespace
{

/** Twelve, once. It was already spelled four times in this file - two of them
    inside a `% 12` that is a pitch class and two of them a transpose limit -
    and fitting the used range to the window needed a fifth.
*/
constexpr int semitonesPerOctave = 12;

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

    // Seeded from what the state already says, so the first broadcast after
    // construction is not mistaken for the channel having changed.
    lastSeenChannelId = editorState.getSelectedChannelId();
    lastSeenPatternId = editorState.getCurrentPatternId();

    horizontalScroll.addListener (this);
    verticalScroll.addListener (this);
    addAndMakeVisible (horizontalScroll);
    addAndMakeVisible (verticalScroll);

    toolbar.onToolChanged = [this]
    {
        // The tool does not change what is selected. Clearing here would throw
        // away the selection the next quantize or transpose is aimed at.
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    };
    toolbar.onSnapChanged = [this] { repaint(); };
    toolbar.onQuantize = [this] { quantizeScope(); };
    toolbar.onRandomize = [this] { openRandomizeDialog(); };
    toolbar.onTranspose = [this] (int semitones) { transposeScope (semitones); };

    toolbar.onChannelChanged = [this] (int channelId)
    {
        editorState.setSelectedChannelId (channelId);
    };

    toolbar.onRowHeight = [this] (double factor) { zoomRowsBy (factor); };

    toolbar.onZoom = [this] (double factor)
    {
        // Zero means "frame the pattern". It used to be reachable only by
        // double-clicking the piano keys, where a double-click already means
        // auditioning the same note twice.
        if (juce::exactlyEqual (factor, 0.0))
        {
            zoomToFit();
            return;
        }

        timeline.zoomAround (factor, contentWidth() * 0.5f);
        updateScrollBars();
        repaint();
    };

    addAndMakeVisible (toolbar);

    rulerGesture.unitForX = [this] (int x)
    {
        return ruler::stepForClick (x, rulerArea(), timeline, numSteps());
    };

    rulerGesture.context = [this]
    {
        ruler::GestureContext ctx;
        ctx.snapUnits = stepsPerBeat();
        ctx.totalUnits = numSteps();
        ctx.playheadUnits = juce::jmax (0.0, engine.getPlayheadSteps());

        return ctx;
    };

    rulerGesture.onSeek = [this] (double steps)
    {
        engine.setPlayheadSteps (steps);
        repaint();
    };

    rulerGesture.onRangeChanged = [this] (juce::Range<int> steps)
    {
        editorState.setSelectedStepRange (steps);
        repaint();
    };

    rulerGesture.onRangeCleared = [this]
    {
        editorState.clearStepSelection();
        repaint();
    };

    updateChannelList();

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
    updateChannelList();
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

void PianoRollComponent::updateChannelList()
{
    juce::StringArray names;
    juce::Array<int> ids;

    for (const auto& channel : document.getState())
        if (channel.hasType (ids::CHANNEL))
        {
            names.add (channel[ids::name].toString());
            ids.add ((int) channel[ids::id]);
        }

    toolbar.setChannels (names, ids);
    toolbar.setSelectedChannel (editorState.getSelectedChannelId());

    // The snap divisions are fractions of a beat, so their labels depend on
    // what a beat is called.
    toolbar.setBeatUnit (Meter::of (document.getState()).beatUnit);
}

int PianoRollComponent::numSteps() const
{
    const auto pattern = currentPattern();
    return pattern.isValid() ? juce::jmax (1, (int) pattern[ids::lengthSteps]) : 16;
}

int PianoRollComponent::stepsPerBeat() const
{
    return juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
}

juce::Colour PianoRollComponent::channelColour() const
{
    const auto channel = ProjectEdits::findChannel (document.getState(),
                                                    editorState.getSelectedChannelId());

    if (! channel.isValid())
        return tokens::colour::accent;

    return channelColour::of (channel);
}

// --- geometry ----------------------------------------------------------------

juce::Rectangle<int> PianoRollComponent::toolbarArea() const
{
    return getLocalBounds().removeFromTop (size::stripToolbar);
}

juce::Rectangle<int> PianoRollComponent::contentArea() const
{
    return getLocalBounds().withTrimmedTop (size::stripToolbar);
}

juce::Rectangle<int> PianoRollComponent::rulerArea() const
{
    return { size::gutterKeyboard, contentArea().getY(),
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness), size::rulerHeight };
}

juce::Rectangle<int> PianoRollComponent::noteArea() const
{
    const auto content = contentArea();
    const auto top = content.getY() + size::rulerHeight;
    const auto bottom = juce::jmax (top, content.getBottom() - size::scrollThickness - velocityHeight);

    return { size::gutterKeyboard, top,
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness), bottom - top };
}

juce::Rectangle<int> PianoRollComponent::keyboardArea() const
{
    const auto notes = noteArea();
    return { 0, notes.getY(), size::gutterKeyboard, notes.getHeight() };
}

juce::Rectangle<int> PianoRollComponent::velocityArea() const
{
    const auto content = contentArea();
    const auto top = juce::jmax (content.getY(), content.getBottom() - size::scrollThickness - velocityHeight);

    return { size::gutterKeyboard, top,
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness), velocityHeight };
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
    return juce::jmax (0, timeline.stepAtX ((float) (x - size::gutterKeyboard)));
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
    const auto x = (float) size::gutterKeyboard + timeline.xForStep ((double) step);
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

// --- tools -------------------------------------------------------------------

int PianoRollComponent::snapSteps() const
{
    const auto meter = Meter::of (document.getState());
    return NoteTools::stepsForSnap (toolbar.getSnap(), meter.stepsPerBeat, meter.beatsPerBar);
}

juce::Array<juce::ValueTree> PianoRollComponent::editScope() const
{
    return NoteTools::scopeFor (currentPattern(), editorState.getSelectedChannelId(), selection);
}

void PianoRollComponent::quantizeScope()
{
    auto pattern = currentPattern();
    const auto scope = editScope();

    if (! pattern.isValid() || scope.isEmpty())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Quantize");
    NoteTools::quantize (pattern, scope, snapSteps(), &undo);
    repaint();
}

void PianoRollComponent::transposeScope (int semitones)
{
    const auto scope = editScope();

    if (scope.isEmpty())
        return;

    auto& undo = document.getUndoManager();

    // Opened only once the group is known to be able to move, so a chord held
    // against the top of the keyboard does not fill the undo stack with edits
    // that changed nothing.
    undo.beginNewTransaction (std::abs (semitones) >= semitonesPerOctave ? "Transpose octave"
                                                                        : "Transpose");

    if (NoteTools::transpose (scope, semitones, lowestPitch, highestPitch, &undo) != 0)
        repaint();
}

void PianoRollComponent::openRandomizeDialog()
{
    const auto scope = editScope();

    if (scope.isEmpty())
        return;

    const auto scopeText = selection.isEmpty()
                               ? "Applies to all " + juce::String (scope.size()) + " notes on this channel"
                               : "Applies to the " + juce::String (scope.size()) + " selected notes";

    RandomizePanel::show (randomizeOptions, scopeText, this,
                          [this] (const NoteTools::RandomizeOptions& options)
                          {
                              randomizeOptions = options;

                              auto pattern = currentPattern();

                              // Re-resolved rather than captured: the dialog is
                              // asynchronous, and the notes it was opened over
                              // may not all still be there.
                              const auto notes = editScope();

                              if (! pattern.isValid() || notes.isEmpty())
                                  return;

                              auto& undo = document.getUndoManager();
                              undo.beginNewTransaction ("Randomize");
                              NoteTools::randomize (pattern, notes, options, random, &undo);
                              repaint();
                          });
}

bool PianoRollComponent::paintNoteAt (juce::Point<int> position)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || ! noteArea().contains (position))
        return false;

    const auto snap = snapSteps();
    const auto step = NoteTools::snapFloor (stepAtX (position.x), snap);
    const auto pitch = pitchAtY (position.y);

    if (step == lastPaintedCell.x && pitch == lastPaintedCell.y)
        return false;

    lastPaintedCell = { step, pitch };

    const auto channelId = editorState.getSelectedChannelId();

    // Anything already sounding at this pitch here, whether it starts in this
    // cell or runs through it - painting over a held note should not stack a
    // second one inside it.
    if (NoteTools::noteCovering (pattern, channelId, step, pitch).isValid())
        return false;

    auto& undo = document.getUndoManager();

    const auto length = juce::jmax (snap, NoteTools::snapCeil (editorState.getLastNoteLengthSteps(), snap));

    auto note = ProjectEdits::addNote (pattern, channelId, step, length, pitch,
                                       (float) editorState.getLastNoteVelocity(), &undo);
    selection.add (note);
    ProjectEdits::growPatternToFitNotes (pattern, &undo);
    return true;
}

void PianoRollComponent::sliceAlong (juce::Point<int> from, juce::Point<int> to)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || from.y == to.y)
        return;   // a horizontal sweep crosses no row's centre, so it cuts nothing

    const auto channelId = editorState.getSelectedChannelId();
    const auto topY = (float) juce::jmin (from.y, to.y);
    const auto bottomY = (float) juce::jmax (from.y, to.y);

    juce::Array<juce::ValueTree> victims;
    juce::Array<int> cuts;

    for (const auto& note : pattern)
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto bounds = boundsForNote (note);
        const auto centreY = bounds.getCentreY();

        // Strictly spanned, so a drag that merely touches a row's edge does not
        // cut the note in it.
        if (centreY <= topY || centreY >= bottomY)
            continue;

        // Where the line is when it crosses this row.
        const auto t = ((double) centreY - from.y) / ((double) to.y - from.y);
        const auto crossingX = (double) from.x + t * ((double) to.x - from.x);

        if (crossingX < bounds.getX() || crossingX > bounds.getRight())
            continue;

        const auto cut = stepAtX ((int) crossingX);
        const auto start = (int) note[ids::step];
        const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);

        if (cut <= start || cut >= start + length)
            continue;

        victims.add (note);
        cuts.add (cut);
    }

    if (victims.isEmpty())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction (victims.size() == 1 ? "Slice note" : "Slice notes");

    // The fragments become the selection: after cutting a run, the next thing
    // you do is almost always to move or delete one side of it.
    selection.clearQuick();

    for (int i = 0; i < victims.size(); ++i)
    {
        auto head = victims.getUnchecked (i);
        auto tail = NoteTools::sliceNote (pattern, head, cuts.getUnchecked (i), &undo);

        if (tail.isValid())
        {
            selection.add (head);
            selection.add (tail);
        }
    }

    repaint();
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

void PianoRollComponent::setRowHeight (int wanted)
{
    const auto clamped = juce::jlimit (size::pianoRowMin, size::pianoRowMax, wanted);

    if (clamped == rowHeight)
        return;

    // Anchor on the pitch in the middle of the note area, for the same reason
    // TimelineView::zoomAround anchors on the pointer: growing the rows from
    // the top walks the music out from under whatever you were looking at.
    //
    // Only when there is something to anchor TO. Ninety-seven rows at the
    // densest height still overflow any window dew will open, so unlike the
    // playlist there is no "it already fits" case - but the guard is written
    // rather than assumed, because a future minimum could make one.
    const auto viewHeight = (double) noteArea().getHeight();
    const auto scrollable = (double) (numRows * rowHeight) > viewHeight;
    const auto anchorRow = (pitchScrollPx + viewHeight * 0.5) / (double) rowHeight;

    rowHeight = clamped;
    pitchScrollPx = scrollable
                      ? juce::jmax (0.0, anchorRow * (double) rowHeight - viewHeight * 0.5)
                      : 0.0;

    // A height change is the user taking the view, exactly as a zoom is -
    // otherwise the next channel change would reframe over it.
    didFitOnce = true;

    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomRowsBy (double factor)
{
    if (factor <= 0.0)
    {
        fitRowsToWindow();
        return;
    }

    setRowHeight ((int) std::lround ((double) rowHeight * factor));
}

void PianoRollComponent::fitRowsToWindow()
{
    // The pitches that are USED, not all ninety-seven: fitting C0 to C8 into a
    // window is a row three pixels tall showing eight octaves of nothing. An
    // empty channel falls back to an octave around where writing would start,
    // which is what scrollToNotesIfOffscreen already picks.
    const auto channelId = editorState.getSelectedChannelId();

    int lowest = highestPitch + 1;
    int highest = lowestPitch - 1;

    for (const auto& note : currentPattern())
        if (note.hasType (ids::NOTE) && (int) note[ids::ch] == channelId)
        {
            const auto pitch = (int) note[ids::pitch];
            lowest = juce::jmin (lowest, pitch);
            highest = juce::jmax (highest, pitch);
        }

    if (highest < lowest)
    {
        const auto channel = ProjectEdits::findChannel (document.getState(), channelId);
        const auto base = channel.isValid() ? (int) channel[ids::basePitch] : 72;

        lowest = base - semitonesPerOctave / 2;
        highest = base + semitonesPerOctave / 2;
    }

    const auto rows = juce::jmax (1, highest - lowest + 1);
    const auto viewHeight = noteArea().getHeight();

    setRowHeight (viewHeight > 0 ? viewHeight / rows : size::pianoRowDefault);
    centreOnPitch ((lowest + highest) / 2);
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& event,
                                         const juce::MouseWheelDetails& wheel)
{
    const auto delta = gesture::deltaOf (wheel);

    // Before isZoom, which a cross-zoom also satisfies.
    if (gesture::isCrossZoom (event.mods))
    {
        zoomRowsBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent));
    }
    else if (gesture::isZoom (event.mods))
    {
        timeline.zoomAround (std::pow (2.0, delta.y * gesture::wheelZoomExponent),
                             (float) (event.x - size::gutterKeyboard));
    }
    else if (event.mods.isShiftDown())
    {
        timeline.scrollOffsetSteps -= delta.along() * gesture::wheelStepsPerNotch;
    }
    else
    {
        // Vertical scrolling walks pitches rather than steps, so a notch is
        // three rows rather than three steps.
        pitchScrollPx -= delta.y * 3.0 * rowHeight;
        timeline.scrollOffsetSteps -= delta.x * gesture::wheelStepsPerNotch;
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

    timeline.zoomAround ((double) scaleFactor, (float) (event.x - size::gutterKeyboard));
    updateScrollBars();
    repaint();
}

bool PianoRollComponent::keyPressed (const juce::KeyPress& key)
{
    // The bindings the three timeline views share are read from one map, so a
    // key means the same thing in whichever tab is in front.
    const auto command = hotkeys::viewCommandFor (key);

    switch (command)
    {
        case hotkeys::ViewCommand::deleteSelection:
            deleteSelection();
            return true;

        case hotkeys::ViewCommand::selectAll:
            selectAllOnChannel();
            return true;

        case hotkeys::ViewCommand::clearSelection:
            selection.clearQuick();
            repaint();
            return true;

        case hotkeys::ViewCommand::zoomIn:
        case hotkeys::ViewCommand::zoomOut:
            timeline.zoomAround (command == hotkeys::ViewCommand::zoomIn ? 1.5 : 1.0 / 1.5,
                                 contentWidth() * 0.5f);
            updateScrollBars();
            repaint();
            return true;

        case hotkeys::ViewCommand::zoomToFit:
            zoomToFit();
            return true;

        case hotkeys::ViewCommand::selectTool: setTool (RollTool::select); return true;
        case hotkeys::ViewCommand::paintTool:  setTool (RollTool::paint);  return true;
        case hotkeys::ViewCommand::eraseTool:  setTool (RollTool::slice);  return true;

        // The other axis: here a row is a semitone.
        case hotkeys::ViewCommand::sizeBigger:
            zoomRowsBy (ZoomButtons::zoomFactor);
            return true;

        case hotkeys::ViewCommand::sizeSmaller:
            zoomRowsBy (1.0 / ZoomButtons::zoomFactor);
            return true;

        case hotkeys::ViewCommand::sizeDefault:
            setRowHeight (size::pianoRowDefault);
            return true;

        case hotkeys::ViewCommand::none:
            break;
    }

    // The arrows and the bare digits are unbound - keyPressed is only reached
    // when the grid itself has focus, so they cannot collide with typing into a
    // number field.
    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        const auto up = key.getKeyCode() == juce::KeyPress::upKey;
        const auto interval = key.getModifiers().isShiftDown() ? 12 : 1;

        transposeScope (up ? interval : -interval);
        return true;
    }

    if (key.getTextCharacter() == 'q' || key.getTextCharacter() == 'Q')
    {
        quantizeScope();
        return true;
    }

    // SHIFT-r, not bare r. Bare r is Record, which the application binds and
    // which has to work from wherever you happen to be looking - and this
    // shadowed it for as long as the roll had focus, silently, because the two
    // key tables could not see each other.
    if ((key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        && key.getModifiers().isShiftDown())
    {
        openRandomizeDialog();
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
        const juce::Point<int> point { juce::roundToInt ((float) from.x + t * (float) (to.x - from.x)),
                                       juce::roundToInt ((float) from.y + t * (float) (to.y - from.y)) };

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

    draggedNote = ProjectEdits::addNote (pattern, editorState.getSelectedChannelId(),
                                         NoteTools::snapFloor (stepAtX (event.x), snap),
                                         juce::jmax (snap, NoteTools::snapCeil (
                                             editorState.getLastNoteLengthSteps(), snap)),
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

    if (gesture == Gesture::resizing)
    {
        // The note's END lands on a grid line, rather than its length becoming
        // a multiple of the grid: a note that already starts off-grid should be
        // draggable to a beat, not merely to a beat's width.
        const auto start = (int) draggedNote[ids::step];
        const auto end = NoteTools::snapCeil (stepAtX (event.x) + 1, snap);

        ProjectEdits::resizeNote (draggedNote, juce::jmax (1, end - start), &undo);
    }
    else if (gesture == Gesture::moving)
    {
        // The grabbed note's absolute target snaps, and the delta that produces
        // is applied to the whole selection. Snapping the delta would leave the
        // note you are holding permanently off the grid; snapping each note
        // separately would collapse a chord's internal offsets onto one step.
        const auto targetStep  = juce::jmax (0, NoteTools::snapNearest (
                                                    stepAtX (event.x) - dragStepOffset, snap));
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

void PianoRollComponent::mouseUp (const juce::MouseEvent& event)
{
    stopAudition();
    draggedVelocityNote = {};

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
    if (draggedNote.isValid() && (gesture == Gesture::resizing || gesture == Gesture::moving))
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

juce::Rectangle<float> PianoRollComponent::velocityBarBounds (const juce::ValueTree& note) const
{
    const auto area = velocityArea();
    const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
    const auto barWidth = (float) juce::jlimit (3.0, 14.0, timeline.pixelsPerStep * 0.7);
    const auto x = (float) size::gutterKeyboard + timeline.xForStep ((double) (int) note[ids::step]);

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
    const auto playing = engine.isPlaying();

    // Playing state is part of the trigger, not a filter on it: a stop that
    // happens not to change the integer step would otherwise leave the last
    // frame on screen.
    if (step != lastPlayheadStep || playing != lastPlaying)
    {
        lastPlayheadStep = step;
        lastPlaying = playing;

        if (playing && engine.getMode() == Transport::Mode::pattern)
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

    // A renamed channel is a channel the selector can no longer be used to
    // find, so the strip follows the name rather than holding the old one.
    if (property == ids::name)
        updateChannelList();

    if (property == ids::beatUnit)
        toolbar.setBeatUnit (Meter::of (document.getState()).beatUnit);

    repaint();
}

void PianoRollComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::CHANNEL))
        updateChannelList();

    repaint();
}

void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    // A selection holding removed notes would delete or move nothing on the next
    // gesture, and looks like the editor ignoring input.
    selection.removeAllInstancesOf (child);

    if (child.hasType (ids::CHANNEL))
        updateChannelList();

    repaint();
}

void PianoRollComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Only when the roll is pointed somewhere ELSE. This used to clear the
    // selection on ANY editor-state change, and EditorState broadcasts for all
    // of them: expanding an effect card, clicking a mixer strip or dragging a
    // span on the playlist ruler each silently threw away a selection the user
    // had built in here. With a time selection now settable from this component
    // too, the roll would also have been fighting its own drag, frame by frame.
    const auto channelId = editorState.getSelectedChannelId();
    const auto patternId = editorState.getCurrentPatternId();

    if (channelId != lastSeenChannelId || patternId != lastSeenPatternId)
    {
        lastSeenChannelId = channelId;
        lastSeenPatternId = patternId;

        // A selection of notes that are no longer on screen is a selection the
        // next gesture would edit invisibly.
        selection.clearQuick();
        scrollToNotesIfOffscreen();
    }

    // Kept in step whoever changed it - the rack, the mixer, the step grid or
    // the strip itself.
    toolbar.setSelectedChannel (channelId);

    repaint();
}

void PianoRollComponent::resized()
{
    toolbar.setBounds (toolbarArea());

    horizontalScroll.setBounds (size::gutterKeyboard, getHeight() - size::scrollThickness,
                                juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
                                size::scrollThickness);

    const auto notes = noteArea();
    verticalScroll.setBounds (getWidth() - size::scrollThickness, notes.getY(), size::scrollThickness, notes.getHeight());

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

        auto colourValue = black ? colour::keyBlack : colour::keyWhite;

        // The key under the pointer lights while it sounds, so a click on the
        // keyboard is visibly doing something and not only audibly.
        if (pitch == auditionPitch)
            colourValue = colour::accent;

        g.setColour (colourValue);
        g.fillRect ((float) keys.getX(), y, (float) (keys.getWidth() - 1), (float) (rowHeight - 1));

        // Every C always, and every key once the rows are tall enough to hold a
        // name without the letters touching. That threshold is what makes a
        // taller row worth having here: at fourteen pixels the strip can only
        // say which octave you are in, and reading a voicing means counting
        // upwards from a C.
        const auto isC = pitch % semitonesPerOctave == 0;

        if (isC || rowHeight >= size::pianoRowRoomy)
        {
            // Dark on a white key, light on a black one. One colour was fine
            // while only C was named, because C is never a black key.
            g.setColour (black ? colour::keyWhite : colour::textOnAccent);
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

    const auto meter = Meter::of (document.getState());

    ruler::Style style;
    style.stepsPerBar = meter.stepsPerBar();
    style.beatsPerBar = meter.beatsPerBar;
    style.totalSteps = numSteps();
    style.playing = engine.isPlaying();

    if (engine.getMode() == Transport::Mode::pattern)
        style.playheadSteps = (double) ((int) engine.getPlayheadSteps() % juce::jmax (1, numSteps()));

    if (editorState.hasStepSelection())
    {
        // Not `selection`: this class has a member of that name, and the CI
        // preset builds with -Werror on -Wshadow.
        const auto steps = editorState.getSelectedStepRange();
        style.selectionStartSteps = (double) steps.getStart();
        style.selectionEndSteps = (double) steps.getEnd();
    }

    // The same ruler the playlist and the channel rack draw. Three views used
    // to hand-roll three of these, and no two of them behaved alike.
    ruler::paint (g, rulerArea(), timeline, style);

    // The corner over the keyboard, which the shared ruler knows nothing about.
    g.setColour (colour::surface);
    g.fillRect (0, 0, size::gutterKeyboard, size::rulerHeight);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (size::rulerHeight - 1, 0.0f, (float) size::gutterKeyboard);
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

    const auto meter = Meter::of (document.getState());
    const auto stepsPerBeat = meter.stepsPerBeat;
    const auto stepsPerBar = meter.stepsPerBar();

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

        g.setColour (pitch % 12 == 0 ? colour::dividerStrong : colour::divider.withAlpha (emphasis::subdued));
        g.drawHorizontalLine ((int) y, (float) area.getX(), (float) area.getRight());
    }

    // --- columns -------------------------------------------------------------
    // The UNCLAMPED range: bar lines carry on to the edge of the window even
    // where the pattern has ended, so the grid never stops mid-view.
    const auto steps = numSteps();
    const auto range = timeline.visibleStepRange (contentWidth());

    timelinePaint::verticalGrid (g, timeline, range, stepsPerBar, stepsPerBeat,
                                 (float) size::gutterKeyboard,
                                 { (float) area.getY(), (float) area.getBottom() },
                                 (float) area.getRight());

    // Past the end of the pattern is still drawn - it is just dimmed, with the
    // end itself marked. It used to be hatched over, which turned every window
    // wider than the music into a dead rectangle.
    const auto endX = (float) size::gutterKeyboard + timeline.xForStep ((double) steps);

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
            g.setColour (colour::dividerStrong.withAlpha (emphasis::subdued));
            g.fillRoundedRectangle (bounds, radius::xs);
            continue;
        }

        // Velocity is visible on the note itself, not only in the lane, so a
        // quiet note reads as quiet while you are writing the melody.
        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);

        // A silent note is stated as faintly as anything else that is there but
        // not sounding; a full-velocity one is stated completely.
        g.setColour (colourForChannel.withAlpha (
            emphasis::subdued + (1.0f - emphasis::subdued) * velocity));
        g.fillRoundedRectangle (bounds, radius::xs);

        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel.brighter (emphasis::edgeLift));
        g.drawRoundedRectangle (bounds, radius::xs,
                                isSelected (note) ? stroke::regular : stroke::hairline);
    }

    // --- position indicator --------------------------------------------------
    // Drawn while stopped as well, dimmed. Hiding it on stop made "reset the
    // position" look identical to "lose the position", and left nothing for a
    // click on the ruler to move.
    if (engine.getMode() == Transport::Mode::pattern)
    {
        const auto playing = engine.isPlaying();
        const auto step = (double) ((int) engine.getPlayheadSteps() % steps);
        const auto x = (float) size::gutterKeyboard + timeline.xForStep (step);

        playhead.set (playing);

        if (playing)
            timelinePaint::playheadColumn (g, { x, (float) area.getY(),
                                                (float) timeline.pixelsPerStep,
                                                (float) area.getHeight() });

        timelinePaint::playheadLine (g, x, { (float) area.getY(), (float) area.getBottom() },
                                     playhead.brightness());
    }

    // --- rubber band ---------------------------------------------------------
    if (gesture == Gesture::selecting && ! rubberBand.isEmpty())
    {
        g.setColour (colour::accent.withAlpha (emphasis::wash));
        g.fillRect (rubberBand);
        g.setColour (colour::accent);
        g.drawRect (rubberBand, stroke::hairlinePx);
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
    paint::sectionHeading (g, { 0, area.getY(), size::gutterKeyboard, area.getHeight() }, "VEL",
                           juce::Justification::centred);

    const auto channelId = editorState.getSelectedChannelId();
    const auto colourForChannel = channelColour();
    const auto barWidth = (float) juce::jlimit (2.0, 14.0, timeline.pixelsPerStep * 0.7);

    const juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area);

    // Bar lines, so a velocity bar can be read against the same grid as the
    // note it belongs to. The lane had no grid at all, which made it hard to
    // tell which bar went with which note once the view was zoomed out.
    const auto meter = Meter::of (document.getState());
    const auto stepsPerBar = meter.stepsPerBar();
    const auto steps = numSteps();

    const auto laneRange = timeline.visibleStepRange (contentWidth());

    for (int step = laneRange.getStart(); step <= laneRange.getEnd(); ++step)
    {
        if (step % stepsPerBar != 0)
            continue;

        const auto lineX = (float) size::gutterKeyboard + timeline.xForStep ((double) step);

        if (lineX > (float) area.getRight())
            break;

        g.setColour (step >= steps ? colour::dividerStrong.withAlpha (emphasis::subdued) : colour::dividerStrong);
        g.drawVerticalLine ((int) lineX, (float) area.getY(), (float) area.getBottom());
    }

    const auto laneEndX = (float) size::gutterKeyboard + timeline.xForStep ((double) steps);

    if (laneEndX < (float) area.getRight())
        paint::beyondEnd (g, juce::Rectangle<float> (laneEndX, (float) area.getY(),
                                                     (float) area.getRight() - laneEndX,
                                                     (float) area.getHeight()).toNearestInt(), laneEndX);

    for (const auto& note : currentPattern())
    {
        if (! note.hasType (ids::NOTE) || (int) note[ids::ch] != channelId)
            continue;

        const auto velocity = (float) juce::jlimit (0.0, 1.0, (double) note[ids::velocity]);
        const auto x = (float) size::gutterKeyboard + timeline.xForStep ((double) (int) note[ids::step]);

        if (x < (float) area.getX() - barWidth || x > (float) area.getRight())
            continue;

        const auto height = velocity * (float) (area.getHeight() - 8);
        const auto bar = juce::Rectangle<float> (x + 1.0f, (float) area.getBottom() - 4.0f - height,
                                                 barWidth, height);

        g.setColour (isSelected (note) ? colour::textPrimary : colourForChannel.withAlpha (emphasis::strong));
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

    // Corner above the keyboard, where the ruler and the gutter meet. Anchored
    // to the content, not to the component: at y = 0 it painted over the tool
    // strip instead of beside the ruler.
    const auto ruler = rulerArea();

    g.setColour (colour::surface);
    g.fillRect (0, ruler.getY(), size::gutterKeyboard, size::rulerHeight);
    g.setColour (colour::dividerStrong);
    g.drawHorizontalLine (ruler.getBottom() - 1, 0.0f, (float) size::gutterKeyboard);

    // The slice line, while it is being drawn. Clipped to the grid so it cannot
    // be mistaken for something that reaches the keyboard or the ruler.
    if (gesture == Gesture::slicing && sliceStart != sliceEnd)
    {
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (noteArea());

        g.setColour (colour::danger);
        g.drawLine ((float) sliceStart.x, (float) sliceStart.y,
                    (float) sliceEnd.x, (float) sliceEnd.y, stroke::bold);
    }

    if (! ProjectEdits::findChannel (document.getState(), editorState.getSelectedChannelId()).isValid())
    {
        paint::emptyState (g, noteArea(), "Select a channel in the Channel Rack");
    }
}

} // namespace dew
