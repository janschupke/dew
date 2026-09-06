// =============================================================================
// The piano roll's view: where things are, and what the two axes are showing.
//
// The same class, a fourth translation unit beside PianoRollPaint.cpp,
// PianoRollGestures.cpp and PianoRollTools.cpp.
//
// The five areas the component divides itself into, the step-to-pixel and
// pitch-to-pixel maps, the note bounds every hit test and every painter reads,
// the row height, the scroll that follows the notes, and the zoom.
//
// Nothing here edits the document. It answers where something IS; the gesture
// file decides what to do about it and the paint file draws it.
// =============================================================================

#include "ui/PianoRollComponent.h"

#include "ui/PianoRollNotes.h"

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "ui/design/Gestures.h"
#include "ui/design/Tokens.h"

namespace dew
{

using namespace tokens;
using namespace pianoRoll;

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
    const auto bottom = juce::jmax (top, content.getBottom() - size::scrollThickness
                                             - velocityLaneHeight);

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
                                 content.getBottom() - size::scrollThickness - velocityLaneHeight);

    return { size::gutterKeyboard, top,
             juce::jmax (0, getWidth() - size::gutterKeyboard - size::scrollThickness),
             velocityLaneHeight };
}

juce::Rectangle<int> PianoRollComponent::velocityResizeArea() const
{
    return velocityArea().withHeight (resizeBandHeight);
}

void PianoRollComponent::setVelocityHeight (int height)
{
    // Clamps whatever it is given, INCLUDING a negative. A drag computes a
    // height by subtraction, so pulling the edge hard down asks for one - and a
    // guard that returned early on it left the lane stuck at whatever it was
    // rather than at its floor. "0 means leave the default" is the settings
    // contract and belongs where the settings arrive, which is EditorTabs.
    //
    // The lane may not take the note grid with it. A window short enough that
    // the range's own floor would leave nothing to write in gets whatever is
    // left, which is the same shape jmax already gives noteArea.
    const auto content = contentArea();
    const auto room = juce::jmax (size::velocityLaneMin, content.getHeight() - size::rulerHeight
                                                             - size::scrollThickness
                                                             - size::pianoRowMin);

    const auto wanted = juce::jlimit (size::velocityLaneMin,
                                      juce::jmin (size::velocityLaneMax, room), height);

    if (std::exchange (velocityLaneHeight, wanted) == wanted)
        return;

    updateScrollBars();
    resized();
    repaint();
}

float PianoRollComponent::contentWidth() const
{
    return (float) noteArea().getWidth();
}

int PianoRollComponent::stepAtX (int x) const
{
    // Deliberately not clamped to the pattern length. Writing past the end is
    // the ONLY way a pattern gets longer - the region beyond is painted as
    // inert so it is clearly outside the pattern, but it is still writable, and
    // doing so refits the pattern around it. Clamping here made
    // fitPatternToNotes unreachable from the mouse.
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

juce::Rectangle<float> PianoRollComponent::boundsForCell (int step, int pitch) const
{
    // One step long, which is what an empty cell is. The keyboard cursor sits
    // on coordinates rather than on notes, so it needs a rectangle for a place
    // that may hold nothing.
    const auto notes = noteArea();

    return { (float) size::gutterKeyboard + timeline.xForStep ((double) step),
             (float) notes.getY() + rows.yForRow (highestPitch - pitch),
             (float) timeline.pixelsPerStep, (float) rows.height };
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

    return (float) position.x >= bounds.getRight() - gesture::rightEdgeBand (bounds.getWidth());
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

void PianoRollComponent::setRowHeight (double wanted)
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

    // And it reads the same modifier map a wheel notch does, rather than
    // ignoring event.mods the way it used to - see PlaylistComponent's.
    if (gesture::intentOf (event.mods) == gesture::WheelIntent::zoomOtherAxis)
    {
        zoomRowsBy ((double) scaleFactor);
        return;
    }

    timeline.zoomAround ((double) scaleFactor, (float) (event.x - size::gutterKeyboard));
    updateScrollBars();
    repaint();
}
} // namespace dew
