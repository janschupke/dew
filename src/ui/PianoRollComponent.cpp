#include "ui/PianoRollComponent.h"

#include "ui/PianoRollNotes.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/Cursors.h"
#include "ui/design/DewLookAndFeel.h"
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

    switch (gesture::intentOf (event.mods))
    {
        case gesture::WheelIntent::zoomOtherAxis:
            zoomRowsBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent));
            break;

        case gesture::WheelIntent::zoomTimeline:
            timeline.zoomAround (std::pow (2.0, delta.y * gesture::wheelZoomExponent),
                                 (float) (event.x - size::gutterKeyboard));
            break;

        case gesture::WheelIntent::scrollTimeline:
            timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.along()
                                                                   * gesture::wheelPixelsPerNotch);
            break;

        case gesture::WheelIntent::scrollBoth:
            // Pixels, not rows. A notch used to be three rows, which was 42px here
            // and one lane - 34px to 204px - in the playlist, for the same flick of
            // the same wheel.
            rows.scrollPx -= delta.y * gesture::wheelPixelsPerNotch;
            timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.x
                                                                   * gesture::wheelPixelsPerNotch);
            break;
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
