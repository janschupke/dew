// =============================================================================
// The playlist's view: where things are, and what the two axes are showing.
//
// The same class, a third translation unit beside PlaylistPaint.cpp and
// PlaylistGestures.cpp.
//
// The lane height and the scroll down the lanes, the bar-to-pixel and
// lane-to-pixel maps, the clip bounds every hit test and every painter reads,
// the zoom, the two scrollbars, and the layout that follows from all of it.
//
// What makes it one file is that nothing here edits the document. It answers
// where something IS; the gesture file decides what to do about it and the
// paint file draws it.
// =============================================================================

#include "ui/PlaylistComponent.h"

#include <cmath>

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/NoteTools.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;

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

void PlaylistComponent::setTrackHeight (double wanted)
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

    // In DOUBLE, because setTrackHeight and RowView::exactHeight both take one:
    // as int / int this quantised the drag by the number of lanes above the
    // grabbed edge, so the fourth lane's edge moved in 4px steps and the tenth
    // in 10px ones - a resize that got coarser the further down you grabbed it.
    setTrackHeight (heightAtDragStart + (double) deltaY / lanes);
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

void PlaylistComponent::zoomStep (double factor)
{
    // Zero means "fit the song", the same shape the piano roll's strip reports.
    // Framing is not a step and does not ease: it is one answer computed from
    // the material, and easing to it would crawl the whole way from wherever
    // the view happened to be.
    if (juce::exactlyEqual (factor, 0.0))
    {
        zoomToFit();
        return;
    }

    // Held for the length of the animation rather than re-derived per frame -
    // the step under the anchor is what each frame solves for, so a moving
    // anchor walks the view.
    zoomAnchorX = contentWidth() * 0.5f;

    // Clamped here as well as in TimelineView: aiming past a stop would spend
    // the rest of the animation's duration arriving at a value the view
    // reached on the first frame.
    zoomMotion.animateTo (juce::jlimit (TimelineView::minPixelsPerStep,
                                        TimelineView::maxPixelsPerStep,
                                        timeline.pixelsPerStep * factor));
}

void PlaylistComponent::heightStep (double factor)
{
    if (factor <= 0.0)
    {
        fitTracksToWindow();
        return;
    }

    heightMotion.animateTo (juce::jlimit ((double) rows.minHeight, (double) rows.maxHeight,
                                          rows.zoomedHeight (factor)));
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

int PlaylistComponent::stepAtX (int x) const
{
    // Not clamped to the song length: dropping a clip past the end is how an
    // arrangement gets longer. The bars beyond are painted inert but stay
    // writable, exactly as the piano roll treats the end of a pattern.
    return juce::jmax (0, timeline.stepAtX ((float) (x - size::gutterTrack)));
}

int PlaylistComponent::barAtX (int x) const
{
    return stepAtX (x) / juce::jmax (1, stepsPerBar());
}

void PlaylistComponent::setToolbarGrid()
{
    const auto meter = Meter::of (document.getState());
    toolbar.setGrid (meter.stepsPerBeat, meter.beatsPerBar, meter.beatUnit);
}

int PlaylistComponent::snapSteps() const
{
    const auto meter = Meter::of (document.getState());
    return NoteTools::stepsForSnap (toolbar.getSnap(), meter.stepsPerBeat, meter.beatsPerBar);
}

int PlaylistComponent::snappedStepAtX (int x, const juce::MouseEvent& event) const
{
    // Shift suspends the grid, which is the rule over the notes and over an
    // automation point, and now over a clip too.
    if (gesture::isFine (event.mods) || ! NoteTools::snaps (toolbar.getSnap()))
        return stepAtX (x);

    return NoteTools::snapFloor (stepAtX (x), snapSteps());
}

int PlaylistComponent::trackAtY (int y) const
{
    // Negative above the lanes, which RowView::rowAtY documents and every
    // caller guards on by testing y < lanesTop() first.
    return rows.rowAtY ((double) (y - lanesTop()));
}

juce::Rectangle<float> PlaylistComponent::boundsForCell (int bar, int trackIndex) const
{
    // One BAR wide, still: the keyboard cursor walks the arrangement a bar at a
    // time, which is the unit somebody navigating with the arrows is thinking
    // in. Only the drawing is in steps.
    const auto perBar = juce::jmax (1, stepsPerBar());

    return { (float) size::gutterTrack + timeline.xForStep ((double) (bar * perBar)),
             laneY (trackIndex), (float) (perBar * timeline.pixelsPerStep), (float) rows.height };
}

juce::Rectangle<float> PlaylistComponent::boundsForClip (const juce::ValueTree& clip,
                                                         int trackIndex) const
{
    const auto start = (int) clip[ids::startStep];
    const auto length = juce::jmax (1, (int) clip[ids::lengthSteps]);

    return { (float) size::gutterTrack + timeline.xForStep ((double) start), laneY (trackIndex),
             (float) (length * timeline.pixelsPerStep), (float) rows.height };
}

bool PlaylistComponent::isOnRightEdge (const juce::ValueTree& clip, int trackIndex,
                                       juce::Point<int> position) const
{
    const auto bounds = boundsForClip (clip, trackIndex);

    return (float) position.x >= bounds.getRight() - gesture::rightEdgeBand (bounds.getWidth());
}

float PlaylistComponent::playheadX() const
{
    // No conversion left to get wrong: the playhead is in steps and so is the
    // timeline it is drawn against.
    return (float) size::gutterTrack + timeline.xForStep (engine.getPlayheadSteps());
}

// --- layout ------------------------------------------------------------------

void PlaylistComponent::zoomToFit()
{
    if (getWidth() <= 0)
        return;

    // Framing on request still counts as taking the view: it is a zoom someone
    // asked for at a size they can see, not the default one.
    viewIsUsers = true;
    timeline.fit (numSteps(), contentWidth());
    updateScrollBar();
    repaint();
}

void PlaylistComponent::applyZoom (double pixelsPerStep)
{
    if (juce::exactlyEqual (pixelsPerStep, timeline.pixelsPerStep))
        return;

    // Expressed as the factor that gets there, so the anchor arithmetic stays
    // TimelineView's and this cannot drift from what a wheel does.
    zoomBy (pixelsPerStep / timeline.pixelsPerStep, zoomAnchorX);
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
    const auto scrollable = visible < (double) numSteps() - 1e-9;

    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (contentWidth(), numSteps());

    // The content is the tracks PLUS the add-track row: fitting or scrolling to
    // a bottom that hid the button that adds the next track would be the same
    // as not having one.
    const auto contentHeight = rows.contentHeight (getNumTracks() + 1);
    const auto laneView = (double) laneViewHeight();

    verticalScroll.setVisible (contentHeight > laneView + 1e-9);

    rows.clampScroll (laneView, getNumTracks() + 1);

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) numSteps(), juce::dontSendNotification);
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
        timeline.fit (numSteps(), contentWidth());

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
    // A pinch reads the same modifier map a wheel notch does. It used to ignore
    // event.mods entirely, so the one gesture on a trackpad that IS a zoom
    // could only ever reach the time axis - while the wheel, which needs a
    // modifier to zoom at all, could reach both.
    //
    // A factor of zero or less would mean "fit", which a pinch cannot ask for.
    if (scaleFactor <= 0.0f)
        return;

    if (gesture::intentOf (event.mods) == gesture::WheelIntent::zoomOtherAxis)
    {
        zoomTracksBy ((double) scaleFactor);
        return;
    }

    zoomBy ((double) scaleFactor, (float) (event.x - size::gutterTrack));
}
} // namespace dew
