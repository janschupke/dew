#include "ui/PlaylistComponent.h"

#include <cmath>
#include <utility>

#include "i18n/Strings.h"
#include "ui/AutomationLane.h"

#include "io/SamplePool.h"

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/TimelineRuler.h"
#include "ui/design/Cursors.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/ButtonBehaviour.h"

namespace dew
{

using namespace tokens;

PlaylistComponent::PlaylistComponent (ProjectDocument& d, AudioEngine& e, EditorState& s,
                                      SamplePool* p)
    : document (d)
    , engine (e)
    , editorState (s)
    , samplePool (p)
{
    setComponentID ("playlist");
    // A name and a PLACE in the tree a screen reader is given. focusContainer,
    // not keyboardFocusContainer: the two flags are independent, and the second
    // would confine the tab key to this panel with no key to leave it - a
    // keyboard trap, which is worse than the flat tab order it would tidy.
    setTitle (tr (StringId::playlist_title));
    setFocusContainerType (FocusContainerType::focusContainer);

    confirmDestructive = confirmWithPanel (this);
    document.getState().addListener (this);
    editorState.addChangeListener (this);

    horizontalScroll.addListener (this);
    addChildComponent (horizontalScroll);

    verticalScroll.addListener (this);
    addChildComponent (verticalScroll);

    // Transparent to the pointer itself so the gutter's own handling is
    // unchanged, but its children still take their clicks.
    headerHolder.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (headerHolder);

    addAutomationButton.setTooltip (tr (StringId::playlist_addAutomation_help));
    addAutomationButton.onClick = [this] { showAutomationMenu(); };
    addAndMakeVisible (addAutomationButton);

    addTrackButton.setTooltip (tr (StringId::playlist_addTrack_help));
    addTrackButton.onClick = [this] { addTrack(); };
    addTrackButton.setComponentID ("addTrackButton");
    headerHolder.addAndMakeVisible (addTrackButton);

    toolbar.onToolChanged = [this] { repaint(); };
    toolbar.onSnapChanged = [this] { repaint(); };
    toolbar.onZoom = [this] (double factor)
    {
        // Zero means "fit the song", the same shape the piano roll's strip
        // reports so the two mean one thing.
        if (juce::exactlyEqual (factor, 0.0))
            zoomToFit();
        else
            zoomBy (factor, contentWidth() * 0.5f);
    };

    toolbar.onTrackHeight = [this] (double factor) { zoomTracksBy (factor); };
    addAndMakeVisible (toolbar);

    setWantsKeyboardFocus (true);

    // The ruler works in STEPS here now, like the two pattern editors, so
    // nothing this gesture is handed needs converting at all - which is one
    // fewer place for the playlist and the transport to disagree about where
    // the playhead is.
    rulerGesture.unitForX = [this] (int x)
    {
        return juce::jlimit (0.0, (double) numSteps(),
                             timeline.stepForX ((float) (x - size::gutterTrack)));
    };

    rulerGesture.context = [this]
    {
        ruler::GestureContext ctx;

        // A BAR, still: a loop range and a render range are sections of an
        // arrangement, and nobody selects three and a half bars to render.
        ctx.snapUnits = stepsPerBar();
        ctx.totalUnits = numSteps();
        ctx.playheadUnits = juce::jmax (0.0, engine.getPlayheadSteps());

        return ctx;
    };

    // The marker as well as the playhead - see ChannelRackComponent's.
    rulerGesture.onSeek = [this] (double steps)
    {
        engine.setStartMarkerSteps (steps);
        repaint();
    };

    rulerGesture.onRangeChanged = [this] (juce::Range<int> steps)
    {
        // Back into BARS, which is what a render range is and what the panel
        // that consumes it shows. The gesture snapped to a bar to get here, so
        // the division is exact.
        const auto perBar = juce::jmax (1, stepsPerBar());

        editorState.setSelectedBarRange (
            { steps.getStart() / perBar, (steps.getEnd() + perBar - 1) / perBar });

        repaint();
    };

    rulerGesture.onRangeCleared = [this]
    {
        editorState.clearBarSelection();
        repaint();
    };

    rebuildHeaders();
    startTimerHz (motion::playheadHz);
}

PlaylistComponent::~PlaylistComponent()
{
    document.getState().removeListener (this);
    editorState.removeChangeListener (this);
}

void PlaylistComponent::refresh()
{
    document.getState().addListener (this);
    rebuildHeaders();
    setToolbarGrid();
    updateScrollBar();
    repaint();
}

// --- model -------------------------------------------------------------------

juce::ValueTree PlaylistComponent::playlist() const
{
    return document.getState().getChildWithName (ids::PLAYLIST);
}

int PlaylistComponent::numBars() const
{
    return juce::jmax (4, (int) document.getState()[ids::barsInSong]);
}

int PlaylistComponent::stepsPerBar() const
{
    return juce::jmax (1, Meter::of (document.getState()).stepsPerBar());
}

int PlaylistComponent::numSteps() const
{
    return numBars() * stepsPerBar();
}

int PlaylistComponent::getNumTracks() const
{
    int count = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
            ++count;

    return count;
}

juce::ValueTree PlaylistComponent::trackAt (int index) const
{
    int i = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK) && i++ == index)
            return track;

    return {};
}

// --- geometry ----------------------------------------------------------------

bool PlaylistComponent::keyPressed (const juce::KeyPress& key)
{
    // One map across the timeline views. The playlist bound four keys and the
    // piano roll bound six of the same ones differently; zoom-to-fit and clear
    // arrive here for the first time because they are in the map, not because
    // anyone remembered to add them twice.
    switch (hotkeys::viewCommandFor (key))
    {
        case hotkeys::ViewCommand::zoomIn:
            zoomBy (ZoomButtons::zoomFactor, contentWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomOut:
            zoomBy (1.0 / ZoomButtons::zoomFactor, contentWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomToFit: zoomToFit(); return true;

        // All three through the register, including the slice key this view has
        // no tool for: applyToolCommand answers false for a tool that is not on
        // this strip, so the key does nothing rather than being a case nobody
        // wrote and nobody could see was missing.
        case hotkeys::ViewCommand::selectTool:
        case hotkeys::ViewCommand::paintTool:
        case hotkeys::ViewCommand::eraseTool:
            return toolbar.applyToolCommand (hotkeys::viewCommandFor (key));

        case hotkeys::ViewCommand::clearSelection:
            editorState.clearBarSelection();
            repaint();
            return true;

        // The other axis: here a row is a track. The same three keys the piano
        // roll uses for a pitch row, and the toolbar's own height buttons.
        case hotkeys::ViewCommand::sizeBigger:
            zoomTracksBy (VerticalZoomButtons::heightFactor);
            return true;

        case hotkeys::ViewCommand::sizeSmaller:
            zoomTracksBy (1.0 / VerticalZoomButtons::heightFactor);
            return true;

        case hotkeys::ViewCommand::sizeDefault:
            setTrackHeight (size::trackHeightDefault);
            return true;

        case hotkeys::ViewCommand::cursorLeft: return moveCursor ({ -1, 0 });
        case hotkeys::ViewCommand::cursorRight: return moveCursor ({ 1, 0 });
        case hotkeys::ViewCommand::cursorUp: return moveCursor ({ 0, -1 });
        case hotkeys::ViewCommand::cursorDown: return moveCursor ({ 0, 1 });

        case hotkeys::ViewCommand::cursorActivate:
            if (! cursor.isPlaced())
                return false;

            activateCursor();
            return true;

        // No note selection to delete and no select-all: a clip is deleted
        // through its own menu. Listed rather than defaulted so adding a
        // command to the map is a compile error here until this view says what
        // it does about it.
        case hotkeys::ViewCommand::deleteSelection:
        case hotkeys::ViewCommand::selectAll:
        case hotkeys::ViewCommand::none: break;
    }

    return false;
}

namespace
{

/** What kind of thing a clip is, in a word. The three kinds are already spelled
    out as strings in the schema and read back in four places; this is the one
    that says them to a person rather than to a switch. */
juce::String clipKindName (const juce::ValueTree& clip)
{
    if (ProjectEdits::isAutomationClip (clip))
        return "automation";

    if (ProjectEdits::isAudioClip (clip))
        return "audio";

    return "pattern";
}

} // namespace

juce::Rectangle<int> PlaylistComponent::cursorLimits() const
{
    auto tracks = 0;

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
            ++tracks;

    return { 0, 0, numBars(), tracks };
}

bool PlaylistComponent::moveCursor (juce::Point<int> delta)
{
    if (! cursor.moveBy (delta, cursorLimits()))
        return true;

    // The playlist's timeline counts BARS, not steps - PlaylistView divides the
    // playhead by stepsPerBar before it gets here - so the cursor's x is
    // already in the unit ensureVisible wants.
    timeline.ensureVisible ((double) cursor.getPosition().x, contentWidth());

    announceCursor();
    repaint();
    return true;
}

void PlaylistComponent::activateCursor()
{
    const auto at = cursor.getPosition();
    const auto track = trackAt (at.y);

    if (! track.isValid())
        return;

    // Opens what is there. A clip is created through the paint tool and deleted
    // through its own menu, so Return is the one thing left that a clip does
    // and that the keyboard could not reach: look inside it.
    if (const auto clip = ProjectEdits::findClipAtStep (track, at.x * stepsPerBar());
        clip.isValid())
        openPatternOf (clip);
}

void PlaylistComponent::announceCursor()
{
    const auto at = cursor.getPosition();
    const auto track = trackAt (at.y);

    if (! track.isValid())
        return;

    auto description = track[ids::name].toString() + ", bar " + juce::String (at.x + 1);

    if (const auto clip = ProjectEdits::findClipAtStep (track, at.x * stepsPerBar());
        clip.isValid())
        description += ", " + clipKindName (clip) + " clip of "
                       + juce::String ((int) clip[ids::lengthSteps]) + " steps";
    else
        description += ", empty";

    setDescription (description);
    juce::AccessibilityHandler::postAnnouncement (
        description, juce::AccessibilityHandler::AnnouncementPriority::low);
}

// --- gestures ----------------------------------------------------------------

void PlaylistComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto trackIndex = trackAtY (event.y);
    const auto track = trackAt (trackIndex);
    const auto clip = track.isValid() ? ProjectEdits::findClipAtStep (track, stepAtX (event.x))
                                      : juce::ValueTree();

    // Driven by the SAME hit test the press uses. Two of them would let the
    // cursor promise something the press then does not do.
    const auto hit = clip.isValid() ? laneHit (clip, trackIndex, event.getPosition())
                                    : automationLane::Hit();

    const auto wasHovered = hoveredSegment;
    hoveredSegment = hit.kind == automationLane::Hit::Kind::segment ? hit.index : -1;
    hoveredSegmentClip = hoveredSegment >= 0 ? clip : juce::ValueTree();

    // Only a CHANGE repaints. Without the guard the whole arrangement repaints
    // on every mouse move, which is the trap lastPaintedCell already guards
    // against for the paint tool.
    if (hoveredSegment != wasHovered)
        repaint();

    if (hit.kind == automationLane::Hit::Kind::point)
    {
        setMouseCursor (cursor::clickable);
        return;
    }

    if (hit.kind == automationLane::Hit::Kind::segment && automationLane::isBendable (hit.point))
    {
        // The cursor DewNumberField already uses for "drag vertically to change
        // a value" - the same gesture, so the same cursor. A stepped segment
        // keeps the normal cursor, because it does not answer to the drag.
        setMouseCursor (cursor::value);
        return;
    }

    if (getRulerArea().contains (event.getPosition()))
    {
        setMouseCursor (cursor::clickable);
        return;
    }

    // A tool outranks what is under the pointer: with the paint tool a clip is
    // not something to pick up, it is somewhere to put one.
    if (getTool() != EditorTool::select)
    {
        setMouseCursor (cursor::nib);
        return;
    }

    if (! clip.isValid())
    {
        setMouseCursor (cursor::idle);
        return;
    }

    // A clip's BODY is draggable, which the cursor never said - the only thing
    // it distinguished was the resize edge, so the gesture a person uses most
    // was the one with no feedback at all.
    setMouseCursor (isOnRightEdge (clip, trackIndex, event.getPosition()) ? cursor::resizeX
                                                                          : cursor::move);
}

void PlaylistComponent::mouseExit (const juce::MouseEvent&)
{
    // Without this the last cursor the arrangement chose survives the pointer
    // leaving it.
    setMouseCursor (cursor::idle);
}

void PlaylistComponent::openPatternOf (const juce::ValueTree& clip)
{
    if (! clip.isValid())
        return;

    // Only a MIDI clip has a pattern. Every clip carries a patternId - the
    // schema keeps all three references on one node - so without this an
    // automation or audio clip would open whatever pattern its unused,
    // defaulted id happened to name.
    if (! ProjectEdits::isMidiClip (clip))
        return;

    const auto patternId = (int) clip[ids::patternId];

    if (! ProjectEdits::findPattern (document.getState(), patternId).isValid())
        return;

    editorState.setCurrentPatternId (patternId);
    engine.setCurrentPatternId (patternId);

    if (onOpenPatternInPianoRoll != nullptr)
        onOpenPatternInPianoRoll();
}

juce::ValueTree PlaylistComponent::automationOf (const juce::ValueTree& clip) const
{
    if (! ProjectEdits::isAutomationClip (clip))
        return {};

    return ProjectEdits::findAutomation (document.getState(), (int) clip[ids::automationId]);
}

automationLane::Geometry PlaylistComponent::laneGeometry (const juce::ValueTree& clip,
                                                          int trackIndex) const
{
    return automationLane::geometryFor (boundsForClip (clip, trackIndex),
                                        juce::jmax (1, (int) clip[ids::lengthSteps]));
}

juce::Point<float> PlaylistComponent::pointPosition (const juce::ValueTree& clip, int trackIndex,
                                                     const juce::ValueTree& point) const
{
    // Kept as a forwarder: it is public, documented as a test seam, and the
    // tests aim at points through it.
    return laneGeometry (clip, trackIndex)
        .positionOf ((double) point[ids::step], (double) point[ids::value]);
}

void PlaylistComponent::positionToCurve (const juce::ValueTree& clip, int trackIndex,
                                         juce::Point<int> position, double& step,
                                         double& value) const
{
    const auto geometry = laneGeometry (clip, trackIndex);

    step = geometry.stepAt ((float) position.x);
    value = geometry.valueAt ((float) position.y);
}

automationLane::Hit PlaylistComponent::laneHit (const juce::ValueTree& clip, int trackIndex,
                                                juce::Point<int> position) const
{
    const auto automation = automationOf (clip);

    if (! automation.isValid())
        return {};

    return automationLane::hitTest (laneGeometry (clip, trackIndex),
                                    ProjectEdits::sortedAutomationPoints (automation),
                                    position.toFloat());
}

juce::ValueTree PlaylistComponent::pointAt (const juce::ValueTree& clip, int trackIndex,
                                            juce::Point<int> position) const
{
    const auto hit = laneHit (clip, trackIndex, position);

    return hit.kind == automationLane::Hit::Kind::point ? hit.point : juce::ValueTree();
}

juce::ValueTree PlaylistComponent::createAutomationClip (const AutomationTarget& target,
                                                         int startBar, int lengthBars)
{
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Add automation");

    // The placing is ProjectEdits' now: every control in the application can ask
    // for a curve, and the arrangement is not on screen for most of them. What
    // is left here is the view catching up with what changed.
    const auto perBar = stepsPerBar();

    auto clip = ProjectEdits::addAutomationWithClip (document.getState(), target, startBar * perBar,
                                                     lengthBars * perBar, &undo);

    updateScrollBar();
    repaint();
    return clip;
}

void PlaylistComponent::timerCallback()
{
    // Not gated on isPlaying any more: the indicator stays put when the
    // transport stops, so Stop visibly returns it to the start rather than
    // making it disappear. Only the wrong MODE has nothing to show.
    if (engine.getMode() != Transport::Mode::song)
    {
        if (lastPaintedPlayheadX >= 0.0f)
        {
            lastPaintedPlayheadX = -1.0f;
            repaint();
        }

        return;
    }

    const auto x = playheadX();

    if (std::abs (x - lastPaintedPlayheadX) < 0.5f && lastPlaying == engine.isPlaying())
        return;

    lastPlaying = engine.isPlaying();

    // Repaint the strip the line was in and the one it moved to, rather than the
    // whole arrangement. Repainting on a bar change was what made the playhead
    // jump a bar at a time instead of moving.
    const auto from = juce::jmin (x, lastPaintedPlayheadX < 0.0f ? x : lastPaintedPlayheadX);
    const auto to = juce::jmax (x, lastPaintedPlayheadX < 0.0f ? x : lastPaintedPlayheadX);

    lastPaintedPlayheadX = x;
    repaint ((int) from - 3, rulerTop(), (int) (to - from) + 7, lanesBottom() - rulerTop());
}

void PlaylistComponent::valueTreePropertyChanged (juce::ValueTree& tree,
                                                  const juce::Identifier& property)
{
    if (property == ids::barsInSong || property == ids::stepsPerBeat
        || property == ids::beatsPerBar)
        updateScrollBar();

    // Any of the three: the unit names the divisions, the resolution decides
    // which of them fall on whole steps, and the beats per bar decide what
    // "Bar" is worth.
    if (property == ids::beatUnit || property == ids::stepsPerBeat || property == ids::beatsPerBar)
        setToolbarGrid();

    if (tree.hasType (ids::PLAYLIST_TRACK))
        for (auto* header : headers)
            header->refresh();

    repaint();
}

void PlaylistComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (ids::PLAYLIST_TRACK) || parent.hasType (ids::PLAYLIST))
        rebuildHeaders();

    repaint();
}

void PlaylistComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (ids::PLAYLIST_TRACK) || parent.hasType (ids::PLAYLIST))
        rebuildHeaders();

    repaint();
}

void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

} // namespace dew
