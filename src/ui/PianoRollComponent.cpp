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
