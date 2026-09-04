#include "ui/PianoRollComponent.h"

#include "ui/PianoRollNotes.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/RandomizePanel.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;
using namespace pianoRoll;

PianoRollComponent::PianoRollComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d)
    , engine (e)
    , editorState (s)
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
        setMouseCursor (cursor::idle);
        repaint();
    };
    toolbar.onSnapChanged = [this] { repaint(); };
    toolbar.onQuantize = [this] { quantizeScope(); };
    toolbar.onRandomize = [this] { openRandomizeDialog(); };
    toolbar.onTranspose = [this] (int semitones) { transposeScope (semitones); };

    toolbar.onChannelChanged = [this] (int channelId)
    { editorState.setSelectedChannelId (channelId); };

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
    { return ruler::stepForClick (x, rulerArea(), timeline, numSteps()); };

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
    rows.scrollPx = (double) ((highestPitch - 72) * rows.height);

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

    return entityColour::of (channel);
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
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
             size::rulerHeight };
}

juce::Rectangle<int> PianoRollComponent::noteArea() const
{
    const auto content = contentArea();
    const auto top = content.getY() + size::rulerHeight;
    const auto bottom = juce::jmax (top,
                                    content.getBottom() - size::scrollThickness - velocityHeight);

    return { size::gutterKeyboard, top,
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
             bottom - top };
}

juce::Rectangle<int> PianoRollComponent::keyboardArea() const
{
    const auto notes = noteArea();
    return { 0, notes.getY(), size::gutterKeyboard, notes.getHeight() };
}

juce::Rectangle<int> PianoRollComponent::velocityArea() const
{
    const auto content = contentArea();
    const auto top = juce::jmax (content.getY(),
                                 content.getBottom() - size::scrollThickness - velocityHeight);

    return { size::gutterKeyboard, top,
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
             velocityHeight };
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
    return highestPitch - (int) (rows.scrollPx / rows.height);
}

int PianoRollComponent::pitchAtY (int y) const
{
    const auto rowsDown = rows.rowAtY ((double) (y - noteArea().getY()));

    return juce::jlimit (lowestPitch, highestPitch, highestPitch - rowsDown);
}

juce::Rectangle<float> PianoRollComponent::boundsForNote (const juce::ValueTree& note) const
{
    const auto step = (int) note[ids::step];
    const auto length = juce::jmax (1, (int) note[ids::lengthSteps]);
    const auto pitch = (int) note[ids::pitch];

    const auto notes = noteArea();
    const auto x = (float) size::gutterKeyboard + timeline.xForStep ((double) step);
    const auto y = (float) notes.getY() + rows.yForRow (highestPitch - pitch);

    return { x, y, (float) (length * timeline.pixelsPerStep), (float) rows.height };
}

bool PianoRollComponent::isOnRightEdge (const juce::ValueTree& note,
                                        juce::Point<int> position) const
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

    const auto scopeText = selection.isEmpty() ? "Applies to all " + juce::String (scope.size())
                                                     + " notes on this channel"
                                               : "Applies to the " + juce::String (scope.size())
                                                     + " selected notes";

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

    const auto length = juce::jmax (
        snap, NoteTools::snapCeil (editorState.getLastNoteLengthSteps(), snap));

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
        return; // a horizontal sweep crosses no row's centre, so it cuts nothing

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

    undo.beginNewTransaction (selection.size() == 1
                                  ? "Delete note"
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

    rows.clampScroll ((double) notes.getHeight(), numRows);

    verticalScroll.setRangeLimits (0.0, rows.contentHeight (numRows), juce::dontSendNotification);
    verticalScroll.setCurrentRange (rows.scrollPx, (double) notes.getHeight(),
                                    juce::dontSendNotification);
}

void PianoRollComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBars)
        return;

    if (bar == &horizontalScroll)
        timeline.scrollOffsetSteps = start;
    else
        rows.scrollPx = start;

    repaint();
}

void PianoRollComponent::centreOnPitch (int pitch)
{
    const auto rowTop = (double) ((highestPitch - juce::jlimit (lowestPitch, highestPitch, pitch))
                                  * rows.height);
    rows.scrollPx = rowTop - noteArea().getHeight() * 0.5 + rows.height * 0.5;
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
    pitchScroll = rows.scrollPx;
}

void PianoRollComponent::applyView (double zoom, double scroll, double pitchScroll)
{
    timeline.pixelsPerStep = juce::jlimit (TimelineView::minPixelsPerStep,
                                           TimelineView::maxPixelsPerStep, zoom);
    timeline.scrollOffsetSteps = juce::jmax (0.0, scroll);
    rows.scrollPx = juce::jmax (0.0, pitchScroll);

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
    // The anchor, the clamp and the "it already fits" case are RowView's, and
    // the playlist's lanes run the same arithmetic. Ninety-seven rows at the
    // densest height still overflow any window dew will open, so the fitting
    // case cannot arise here - but it is RowView's to handle rather than an
    // assumption written into one of its two callers.
    if (! rows.setHeight (wanted, (double) noteArea().getHeight(), numRows))
        return;

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

    setRowHeight (rows.zoomedHeight (factor));
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

    setRowHeight (rows.heightToFit (highest - lowest + 1, (double) noteArea().getHeight(),
                                    size::pianoRowDefault));
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
        timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.along()
                                                               * gesture::wheelPixelsPerNotch);
    }
    else
    {
        // Pixels, not rows. A notch used to be three rows, which was 42px here
        // and one lane - 34px to 204px - in the playlist, for the same flick of
        // the same wheel.
        rows.scrollPx -= delta.y * gesture::wheelPixelsPerNotch;
        timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.x
                                                               * gesture::wheelPixelsPerNotch);
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
        case hotkeys::ViewCommand::deleteSelection: deleteSelection(); return true;

        case hotkeys::ViewCommand::selectAll: selectAllOnChannel(); return true;

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

        case hotkeys::ViewCommand::zoomToFit: zoomToFit(); return true;

        case hotkeys::ViewCommand::selectTool: setTool (RollTool::select); return true;
        case hotkeys::ViewCommand::paintTool: setTool (RollTool::paint); return true;
        case hotkeys::ViewCommand::eraseTool: setTool (RollTool::slice); return true;

        // The other axis: here a row is a semitone.
        case hotkeys::ViewCommand::sizeBigger: zoomRowsBy (ZoomButtons::zoomFactor); return true;

        case hotkeys::ViewCommand::sizeSmaller:
            zoomRowsBy (1.0 / ZoomButtons::zoomFactor);
            return true;

        case hotkeys::ViewCommand::sizeDefault: setRowHeight (size::pianoRowDefault); return true;

        case hotkeys::ViewCommand::none: break;
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
        return cursor::value;

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
    const auto x = (float) size::gutterKeyboard
                   + timeline.xForStep ((double) (int) note[ids::step]);

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
        const auto column = juce::Rectangle<float> (
            bar.getX() - 2.0f, (float) velocityArea().getY(), bar.getWidth() + 4.0f,
            (float) velocityArea().getHeight());

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
            && (int) note[ids::step] == step && (selection.isEmpty() || isSelected (note)))
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

void PianoRollComponent::valueTreePropertyChanged (juce::ValueTree&,
                                                   const juce::Identifier& property)
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

    horizontalScroll.setBounds (
        size::gutterKeyboard, getHeight() - size::scrollThickness,
        juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
        size::scrollThickness);

    const auto notes = noteArea();
    verticalScroll.setBounds (getWidth() - size::scrollThickness, notes.getY(),
                              size::scrollThickness, notes.getHeight());

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

} // namespace dew
