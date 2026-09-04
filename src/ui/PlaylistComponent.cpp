#include "ui/PlaylistComponent.h"

#include <cmath>
#include <utility>

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
#include "ui/primitives/DewControls.h"

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

    addAutomationButton.setTooltip ("Add an automation lane for a parameter");
    addAutomationButton.onClick = [this] { showAutomationMenu(); };
    addAndMakeVisible (addAutomationButton);

    addTrackButton.setTooltip ("Add a track to the arrangement");
    addTrackButton.onClick = [this] { addTrack(); };
    addTrackButton.setComponentID ("addTrackButton");
    headerHolder.addAndMakeVisible (addTrackButton);

    toolbar.onToolChanged = [this] { repaint(); };
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

    // The ruler works in BARS here, so everything the gesture is handed is in
    // bars and only the seek converts - one place, rather than a conversion in
    // each of the three branches this used to have.
    rulerGesture.unitForX = [this] (int x)
    {
        return juce::jlimit (0.0, (double) numBars(),
                             timeline.stepForX ((float) (x - size::gutterTrack)));
    };

    rulerGesture.context = [this]
    {
        const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

        ruler::GestureContext ctx;
        ctx.snapUnits = 1;
        ctx.totalUnits = numBars();
        ctx.playheadUnits = juce::jmax (0.0, engine.getPlayheadSteps() / (double) stepsPerBar);

        return ctx;
    };

    rulerGesture.onSeek = [this] (double bars)
    {
        const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();

        engine.setPlayheadSteps (bars * (double) stepsPerBar);
        repaint();
    };

    rulerGesture.onRangeChanged = [this] (juce::Range<int> bars)
    {
        editorState.setSelectedBarRange (bars);
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

void PlaylistComponent::scrollTracksTo (double offsetPx)
{
    rows.scrollPx = juce::jmax (0.0, offsetPx);
    viewIsUsers = true;

    // updateScrollBar re-clamps against the content height, so an offset past
    // the end lands on the end rather than off it.
    updateScrollBar();
    resized();
    repaint();
}

void PlaylistComponent::setTrackHeight (int wanted)
{
    // The clamp, the anchor on the middle of the view, and the "already fits"
    // case are RowView's, and the piano roll's pitch rows run the same
    // arithmetic. The add-track row counts as a lane: scrolling to a bottom
    // that hid the button that adds the next track would be the same as not
    // having one.
    if (! rows.setHeight (wanted, (double) laneViewHeight(), getNumTracks() + 1))
        return;

    // The one thing that is this view's alone: NOT re-centring during a resize
    // drag, where the anchor is the edge under the pointer rather than the
    // middle of the view. Re-centring between drag samples moves the grabbed
    // edge away from the hand holding it, which is the one thing a drag must
    // not do. So the scroll RowView just computed is discarded for the one it
    // was held at when the drag began.
    if (resizingRows)
        rows.scrollPx = heightDragScrollPx;

    // A height change is the user taking the view, exactly as a zoom is -
    // otherwise the next resize would re-fit and undo it.
    viewIsUsers = true;

    updateScrollBar();
    resized();
    repaint();
}

void PlaylistComponent::beginRowHeightDrag()
{
    heightAtDragStart = rows.height;
    heightDragScrollPx = rows.scrollPx;
    resizingRows = true;
}

void PlaylistComponent::dragRowHeightBy (int laneIndex, int deltaY)
{
    if (! resizingRows)
        return;

    // Absolute, from where the press was, rather than accumulated between
    // samples: the same reason the effect chain's reorder hit-tests against
    // frozen bounds. A path-dependent drag drifts, and cannot be asserted by
    // sampling the same interaction two ways.
    //
    // Divided by the lanes ABOVE the grabbed edge as well as the grabbed one,
    // because they all grow together - so the edge under the pointer is the one
    // that follows it, whichever lane it belongs to.
    const auto lanes = juce::jmax (1, laneIndex + 1);

    setTrackHeight (heightAtDragStart + deltaY / lanes);
}

void PlaylistComponent::endRowHeightDrag()
{
    resizingRows = false;

    // The scroll was held through the drag; let it settle against the height it
    // ended on, which is what every other path already does.
    updateScrollBar();
    repaint();
}

void PlaylistComponent::zoomTracksBy (double factor)
{
    if (factor <= 0.0)
    {
        fitTracksToWindow();
        return;
    }

    setTrackHeight (rows.zoomedHeight (factor));
}

void PlaylistComponent::fitTracksToWindow()
{
    // The add-track row counts: fitting to the tracks alone would push the
    // button that adds the next one just off the bottom.
    //
    // Zero is the fallback for an unlaid-out view because that is what the
    // integer division answered before, and setTrackHeight clamps it up to the
    // shortest lane on the ladder.
    setTrackHeight (rows.heightToFit (getNumTracks() + 1, (double) laneViewHeight(), 0));
}

int PlaylistComponent::tracksBottom() const
{
    return (int) laneY (getNumTracks());
}

int PlaylistComponent::lanesBottom() const
{
    // The jmin, not viewBottom(): with the tracks fitting, this is exactly where
    // the last one ends, which is where the grid and the playhead have always
    // stopped. Taking the view's bottom instead would extend every one of them
    // over empty space the moment a lane could be scrolled.
    return juce::jmin (tracksBottom(), viewBottom());
}

juce::Rectangle<int> PlaylistComponent::getLaneArea() const
{
    return { size::gutterTrack, lanesTop(), (int) contentWidth(), laneViewHeight() };
}

float PlaylistComponent::contentWidth() const
{
    // The vertical scrollbar's strip is reserved whether or not the bar is
    // showing, the way the piano roll's note area reserves its own. Giving it
    // back when the tracks happen to fit would re-lay the arrangement
    // HORIZONTALLY every time a track was added.
    return (float) juce::jmax (0, getWidth() - size::gutterTrack - size::scrollThickness);
}

int PlaylistComponent::barAtX (int x) const
{
    // Not clamped to the song length: dropping a clip past the end is how an
    // arrangement gets longer. The bars beyond are painted inert but stay
    // writable, exactly as the piano roll treats the end of a pattern.
    return juce::jmax (0, timeline.stepAtX ((float) (x - size::gutterTrack)));
}

int PlaylistComponent::trackAtY (int y) const
{
    // Negative above the lanes, which RowView::rowAtY documents and every
    // caller guards on by testing y < lanesTop() first.
    return rows.rowAtY ((double) (y - lanesTop()));
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip,
                                                         int trackIndex) const
{
    const auto start = (int) clip[ids::startBar];
    const auto length = juce::jmax (1, (int) clip[ids::lengthBars]);

    return { (float) size::gutterTrack + timeline.xForStep ((double) start), laneY (trackIndex),
             (float) (length * timeline.pixelsPerStep), (float) rows.height };
}

bool PlaylistComponent::isOnRightEdge (const juce::ValueTree& clip, int trackIndex,
                                       juce::Point<int> position) const
{
    const auto bounds = boundsForClip (clip, trackIndex);
    const auto edge = juce::jmin (10.0f, bounds.getWidth() * 0.3f);

    return (float) position.x >= bounds.getRight() - edge;
}

float PlaylistComponent::playheadX() const
{
    const auto stepsPerBar = Meter::of (document.getState()).stepsPerBar();
    const auto position = engine.getPlayheadSteps() / (double) stepsPerBar;

    return (float) size::gutterTrack + timeline.xForStep (position);
}

// --- layout ------------------------------------------------------------------

void PlaylistComponent::zoomToFit()
{
    if (getWidth() <= 0)
        return;

    // Framing on request still counts as taking the view: it is a zoom someone
    // asked for at a size they can see, not the default one.
    viewIsUsers = true;
    timeline.fit (numBars(), contentWidth());
    updateScrollBar();
    repaint();
}

void PlaylistComponent::zoomBy (double factor, float anchorX)
{
    viewIsUsers = true;
    timeline.zoomAround (factor, anchorX);
    updateScrollBar();
    repaint();
}

void PlaylistComponent::updateScrollBar()
{
    if (getWidth() <= 0)
        return;

    // The scroll range reaches a screen PAST the song, which is what
    // TimelineView::clampScroll already allows and what the piano roll has
    // always done. Pinning it to the song's length is what made the empty
    // space beyond the last bar unreachable - there was nowhere to scroll to.
    const auto visible = timeline.visibleSteps (contentWidth());
    const auto scrollable = visible < (double) numBars() - 1e-9;

    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (contentWidth(), numBars());

    // The content is the tracks PLUS the add-track row: fitting or scrolling to
    // a bottom that hid the button that adds the next track would be the same
    // as not having one.
    const auto contentHeight = rows.contentHeight (getNumTracks() + 1);
    const auto laneView = (double) laneViewHeight();

    verticalScroll.setVisible (contentHeight > laneView + 1e-9);

    rows.clampScroll (laneView, getNumTracks() + 1);

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) numBars(), juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps, visible,
                                      juce::dontSendNotification);
    verticalScroll.setRangeLimits (0.0, contentHeight, juce::dontSendNotification);
    verticalScroll.setCurrentRange (rows.scrollPx, laneView, juce::dontSendNotification);
}

void PlaylistComponent::rebuildHeaders()
{
    headers.clear();

    for (const auto& track : playlist())
        if (track.hasType (ids::PLAYLIST_TRACK))
        {
            auto* header = headers.add (new PlaylistTrackHeader (document, track));
            header->onAddTrack = [this] { addTrack(); };
            header->onRemoveTrack = [this] (juce::ValueTree t) { removeTrack (t); };
            header->onResetHeight = [this] { setTrackHeight (size::trackHeightDefault); };
            header->onResizeBegin = [this] { beginRowHeightDrag(); };
            header->onResizeDrag = [this] (int lane, int dy) { dragRowHeightBy (lane, dy); };
            header->onResizeEnd = [this] { endRowHeightDrag(); };
            header->setIndex (headers.size() - 1);
            headerHolder.addAndMakeVisible (header);
        }

    resized();
}

void PlaylistComponent::resized()
{
    toolbar.setBounds (0, 0, getWidth(), size::stripToolbar);

    horizontalScroll.setBounds (size::gutterTrack, getHeight() - size::scrollThickness,
                                (int) contentWidth(), size::scrollThickness);

    verticalScroll.setBounds (getWidth() - size::scrollThickness, lanesTop(), size::scrollThickness,
                              laneViewHeight());

    // In the corner above the track headers, where the ruler does not reach.
    addAutomationButton.setBounds (
        juce::Rectangle<int> (0, rulerTop(), size::gutterTrack, size::rulerHeight)
            .reduced (space::xs, space::xxs));

    // The holder spans the lane strip; its children sit in ITS coordinates,
    // which is laneY minus the strip's own top.
    headerHolder.setBounds (0, lanesTop(), size::gutterTrack, laneViewHeight());

    const auto rowTop = [this] (int index) { return (int) laneY (index) - lanesTop(); };

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rowTop (i), size::gutterTrack, rows.height);

    // Directly below the last track: the next empty row of the list, where the
    // track it adds will appear. Always present now rather than hidden when the
    // window ran out - it is reachable by scrolling, and an add button that
    // disappears once you have enough tracks is the bug the channel rack already
    // had and fixed.
    //
    // At the TOP of that row and never taller than one rung, because a lane
    // height is a property of the arrangement and this is a button. It used to
    // take the whole row, so at the tallest lane height "+ Track" was a
    // two-hundred-pixel rectangle.
    const auto addRow = juce::Rectangle<int> (0, rowTop (headers.size()), size::gutterTrack,
                                              rows.height);

    addTrackButton.setBounds (addRow.withHeight (juce::jmin (rows.height, size::rowHeight))
                                  .reduced (space::sm, space::xs));

    // Until someone has zoomed or scrolled, a layout frames the whole song.
    // Latching after the FIRST layout instead would hand the zoom to whatever
    // size the component happened to be built at, which is not the size it ends
    // up; re-fitting always is what made the playlist unzoomable.
    if (! viewIsUsers && getWidth() > 0)
        timeline.fit (numBars(), contentWidth());

    updateScrollBar();
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (updatingScrollBar)
        return;

    viewIsUsers = true;

    if (bar == &verticalScroll)
    {
        // The headers are laid out from laneY, so they follow the offset only
        // when something re-lays them - resized() rather than a repaint.
        scrollTracksTo (start);
        return;
    }

    timeline.scrollOffsetSteps = start;
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& event,
                                        const juce::MouseWheelDetails& wheel)
{
    // Mod-wheel zooms around the pointer, exactly as it does over the piano
    // roll. The playlist answered only to scrolling, and only when there was
    // something to scroll.
    const auto delta = gesture::deltaOf (wheel);
    const auto intent = gesture::intentOf (event.mods);

    if (intent == gesture::WheelIntent::zoomOtherAxis)
    {
        zoomTracksBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent));
        return;
    }

    if (intent == gesture::WheelIntent::zoomTimeline)
    {
        zoomBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent),
                (float) (event.x - size::gutterTrack));
        return;
    }

    viewIsUsers = true;

    // The piano roll's convention, adopted here because the playlist now has
    // two axes to scroll and had only ever had one: shift scrolls time, and a
    // plain wheel scrolls the TRACKS.
    //
    // This changes a shipped gesture - a plain wheel used to scroll time - and
    // it is the change a user notices first. It is still the right one: the two
    // timeline views disagreed about what a plain wheel meant, and the axis the
    // wheel naturally maps to is the one that now moves.
    if (intent == gesture::WheelIntent::scrollTimeline)
    {
        timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.along()
                                                               * gesture::wheelPixelsPerNotch);
        updateScrollBar();
        repaint();
        return;
    }

    timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.x * gesture::wheelPixelsPerNotch);

    // Pixels, not lanes. A notch used to move exactly one lane, so the same
    // gesture travelled 34px or 204px depending on a height this very wheel
    // can change.
    scrollTracksTo (rows.scrollPx - delta.y * gesture::wheelPixelsPerNotch);
}

void PlaylistComponent::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    zoomBy ((double) scaleFactor, (float) (event.x - size::gutterTrack));
}

bool PlaylistComponent::keyPressed (const juce::KeyPress& key)
{
    // One map across the timeline views. The playlist bound four keys and the
    // piano roll bound six of the same ones differently; zoom-to-fit and clear
    // arrive here for the first time because they are in the map, not because
    // anyone remembered to add them twice.
    switch (hotkeys::viewCommandFor (key))
    {
        case hotkeys::ViewCommand::zoomIn: zoomBy (1.5, contentWidth() * 0.5f); return true;

        case hotkeys::ViewCommand::zoomOut: zoomBy (1.0 / 1.5, contentWidth() * 0.5f); return true;

        case hotkeys::ViewCommand::zoomToFit: zoomToFit(); return true;

        case hotkeys::ViewCommand::selectTool: toolbar.setTool (PlaylistTool::select); return true;

        case hotkeys::ViewCommand::paintTool: toolbar.setTool (PlaylistTool::paint); return true;

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

        // The playlist has no erase tool, no note selection to delete and no
        // select-all: a clip is deleted through its own menu. Listed rather
        // than defaulted so adding a command to the map is a compile error
        // here until this view says what it does about it.
        case hotkeys::ViewCommand::eraseTool:
        case hotkeys::ViewCommand::deleteSelection:
        case hotkeys::ViewCommand::selectAll:
        case hotkeys::ViewCommand::none: break;
    }

    return false;
}

// --- gestures ----------------------------------------------------------------

void PlaylistComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto trackIndex = trackAtY (event.y);
    const auto track = trackAt (trackIndex);
    const auto clip = track.isValid() ? ProjectEdits::findClipAtBar (track, barAtX (event.x))
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
    if (getTool() != PlaylistTool::select)
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
                                        juce::jmax (1, (int) clip[ids::lengthBars]),
                                        Meter::of (document.getState()).stepsPerBar());
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
    auto clip = ProjectEdits::addAutomationWithClip (document.getState(), target, startBar,
                                                     lengthBars, &undo);

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
    if (property == ids::barsInSong)
        updateScrollBar();

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
