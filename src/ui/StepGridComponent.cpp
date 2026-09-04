#include "ui/StepGridComponent.h"

#include "io/SamplePool.h"

#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/design/Gestures.h"
#include "ui/Hotkeys.h"
#include "ui/ZoomButtons.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Cursors.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

StepGridComponent::StepGridComponent (ProjectDocument& d, AudioEngine& e, EditorState& s,
                                      SamplePool* p)
    : document (d)
    , engine (e)
    , editorState (s)
    , samplePool (p)
{
    setComponentID ("stepGrid");

    // The grid has overridden keyPressed since it was written, and nothing ever
    // asked for focus - so its zoom keys were live in the tests and nowhere
    // else, and the channel rack was the one tab with no key handling at all.
    setWantsKeyboardFocus (true);

    horizontalScroll.addListener (this);
    addChildComponent (horizontalScroll);
    startTimerHz (motion::playheadHz);
}

StepGridComponent::~StepGridComponent() = default;

juce::ValueTree StepGridComponent::currentPattern() const
{
    return ProjectEdits::findPattern (document.getState(), editorState.getCurrentPatternId());
}

int StepGridComponent::numSteps() const
{
    const auto pattern = currentPattern();
    return pattern.isValid() ? juce::jmax (1, (int) pattern[ids::lengthSteps]) : 16;
}

int StepGridComponent::getNumSteps() const
{
    return numSteps();
}

void StepGridComponent::updateZoom()
{
    const auto steps = numSteps();
    const auto width = (float) getWidth();

    if (width <= 0.0f)
        return;

    // Fill the width when the pattern can, and fall back to scrolling when a
    // step would otherwise be too narrow to aim at. A 16-step pattern fits; a
    // 128-step one scrolls at a workable cell size instead of becoming hairlines.
    //
    // Only until someone says otherwise. Auto-fit used to be a law rather than
    // a default: the grid re-derived its zoom on every layout and on every
    // pattern-length change, so a zoom you chose was thrown away by the next
    // thing that happened. The playlist already worked this way.
    if (! viewIsUsers)
        timeline.pixelsPerStep = juce::jlimit (TimelineView::minPixelsPerStep,
                                               TimelineView::maxPixelsPerStep,
                                               (double) width / (double) steps);

    const auto scrollable = isScrollable();
    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (width, steps);

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) steps, juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps, timeline.visibleSteps (width),
                                      juce::dontSendNotification);

    if (onTimelineChanged != nullptr)
        onTimelineChanged();
}

bool StepGridComponent::isScrollable() const
{
    return timeline.visibleSteps ((float) getWidth()) < (double) numSteps() - 1e-9;
}

void StepGridComponent::resized()
{
    horizontalScroll.setBounds (0, getHeight() - size::scrollThickness, getWidth(),
                                size::scrollThickness);
    updateZoom();
}

void StepGridComponent::scrollBarMoved (juce::ScrollBar*, double start)
{
    if (updatingScrollBar)
        return;

    timeline.scrollOffsetSteps = start;
    repaint();

    if (onTimelineChanged != nullptr)
        onTimelineChanged();
}

void StepGridComponent::mouseWheelMove (const juce::MouseEvent& event,
                                        const juce::MouseWheelDetails& wheel)
{
    const auto delta = gesture::deltaOf (wheel);

    if (gesture::isZoom (event.mods))
    {
        zoomBy (std::pow (2.0, delta.y * gesture::wheelZoomExponent), (float) event.x);
        return;
    }

    if (! isScrollable())
        return;

    timeline.scrollOffsetSteps -= timeline.stepsForPixels (delta.along()
                                                           * gesture::wheelPixelsPerNotch);
    updateZoom();
    repaint();
}

void StepGridComponent::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    zoomBy ((double) scaleFactor, (float) event.x);
}

void StepGridComponent::zoomBy (double factor, float anchorX)
{
    // Taking the view: from here on the grid keeps the zoom it was given rather
    // than re-fitting itself on the next layout.
    viewIsUsers = true;
    timeline.zoomAround (factor, anchorX);
    updateZoom();
    repaint();
}

void StepGridComponent::zoomToFit()
{
    if (getWidth() <= 0)
        return;

    // Framing on request still counts as taking the view: it is a zoom someone
    // asked for at a size they can see, not the default one.
    viewIsUsers = true;
    timeline.fit (numSteps(), (float) getWidth());
    updateZoom();
    repaint();
}

juce::Rectangle<int> StepGridComponent::cursorLimits() const
{
    return { 0, 0, numSteps(), getNumRows() };
}

bool StepGridComponent::moveCursor (juce::Point<int> delta)
{
    const auto previous = cursor.getPosition();

    if (! cursor.moveBy (delta, cursorLimits()))
        return true; // at the edge: the key was ours, it simply had nowhere to go

    repaintCell (previous);
    repaintCell (cursor.getPosition());

    // The channel under the cursor becomes the selected one, so the instrument
    // panel follows the keyboard the way it follows a click.
    if (const auto channel = channelForRow (cursor.getPosition().y); channel.isValid())
        editorState.setSelectedChannelId ((int) channel[ids::id]);

    announceCursor();
    return true;
}

void StepGridComponent::activateCursor()
{
    const auto channel = channelForRow (cursor.getPosition().y);

    if (! channel.isValid())
        return;

    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("Toggle step");

    ProjectEdits::toggleStep (currentPattern(), (int) channel[ids::id], cursor.getPosition().x,
                              (int) channel[ids::basePitch], &undo);

    announceCursor();
}

void StepGridComponent::announceCursor()
{
    const auto channel = channelForRow (cursor.getPosition().y);

    if (! channel.isValid())
        return;

    const auto step = cursor.getPosition().x;
    const auto lit = ProjectEdits::findNoteAtStep (currentPattern(), (int) channel[ids::id], step)
                         .isValid();

    // The channel, where in the bar, and whether it sounds - which is the whole
    // of what a step is. Said as a sentence rather than as coordinates: "step 5"
    // is a number, "beat 2 of bar 1" is a place in the music.
    const auto meter = Meter::of (document.getState());
    const auto perBar = juce::jmax (1, meter.stepsPerBar());

    const auto description = channel[ids::name].toString() + ", bar "
                             + juce::String (step / perBar + 1) + " step "
                             + juce::String (step % perBar + 1) + ", " + (lit ? "on" : "off");

    setDescription (description);
    juce::AccessibilityHandler::postAnnouncement (
        description, juce::AccessibilityHandler::AnnouncementPriority::low);
}

bool StepGridComponent::keyPressed (const juce::KeyPress& key)
{
    switch (hotkeys::viewCommandFor (key))
    {
        case hotkeys::ViewCommand::zoomIn:
            zoomBy (ZoomButtons::zoomFactor, (float) getWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomOut:
            zoomBy (1.0 / ZoomButtons::zoomFactor, (float) getWidth() * 0.5f);
            return true;

        case hotkeys::ViewCommand::zoomToFit: zoomToFit(); return true;

        case hotkeys::ViewCommand::cursorLeft: return moveCursor ({ -1, 0 });
        case hotkeys::ViewCommand::cursorRight: return moveCursor ({ 1, 0 });
        case hotkeys::ViewCommand::cursorUp: return moveCursor ({ 0, -1 });
        case hotkeys::ViewCommand::cursorDown: return moveCursor ({ 0, 1 });

        case hotkeys::ViewCommand::cursorActivate:
            if (! cursor.isPlaced())
                return false;

            activateCursor();
            return true;

        // The sequencer has no tools, no note selection and no select-all: a
        // step is toggled, not selected. Nor a second size: a row here is a
        // CHANNEL, and how tall one is belongs to the rack that lists them, not
        // to the grid painted beside it. Listed rather than defaulted, so a new
        // command is a compile error here until this view says what it does.
        case hotkeys::ViewCommand::sizeBigger:
        case hotkeys::ViewCommand::sizeSmaller:
        case hotkeys::ViewCommand::sizeDefault:
        case hotkeys::ViewCommand::selectTool:
        case hotkeys::ViewCommand::paintTool:
        case hotkeys::ViewCommand::eraseTool:
        case hotkeys::ViewCommand::clearSelection:
        case hotkeys::ViewCommand::deleteSelection:
        case hotkeys::ViewCommand::selectAll:
        case hotkeys::ViewCommand::none: break;
    }

    return false;
}

int StepGridComponent::stepAtX (int x) const
{
    return juce::jlimit (0, numSteps() - 1, timeline.stepAtX ((float) x));
}

int StepGridComponent::rowAtY (int y) const
{
    return y / size::rowHeight;
}

juce::Rectangle<float> StepGridComponent::getBoundsForCell (int row, int step) const
{
    return { timeline.xForStep ((double) step), (float) (row * size::rowHeight),
             (float) timeline.pixelsPerStep, (float) size::rowHeight };
}

int StepGridComponent::getNumRows() const
{
    int rows = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::CHANNEL))
            ++rows;

    return rows;
}

int StepGridComponent::getRowsHeight() const
{
    return getNumRows() * size::rowHeight;
}

juce::ValueTree StepGridComponent::channelForRow (int row) const
{
    int index = 0;

    for (const auto& channel : document.getState())
        if (channel.hasType (ids::CHANNEL) && index++ == row)
            return channel;

    return {};
}

void StepGridComponent::timerCallback()
{
    // Editing the pattern length changes how many steps have to fit, and the
    // zoom is derived from that. Nothing resizes the component when it happens,
    // so the grid would keep drawing at the old cell width until the next layout.
    if (const auto steps = numSteps(); steps != lastLayoutSteps)
    {
        lastLayoutSteps = steps;
        updateZoom();
        repaint();
    }

    const auto step = (int) engine.getPlayheadSteps();
    const auto playing = engine.isPlaying();

    // The playing state has to be part of the trigger, not a filter on it.
    // This used to see the step drop to zero on stop, update lastPlayheadStep,
    // and then swallow the repaint because isPlaying() was already false - so
    // the last painted frame stayed on screen until the next Play. That is
    // exactly "the indicator only resets when you press play again".
    if (step != lastPlayheadStep || playing != lastPlaying)
    {
        lastPlayheadStep = step;
        lastPlaying = playing;
        repaint (0, 0, getWidth(), getRowsHeight());
    }
}

void StepGridComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto cell = juce::Point<int> (stepAtX (event.x), rowAtY (event.y));
    const auto channel = channelForRow (cell.y);

    // No cell highlight on a row that takes no notes: the hover exists to say
    // "a click lands here", and on a waveform row it does not.
    const auto valid = event.y < getRowsHeight() && channel.isValid()
                       && ProjectEdits::playsNotes (channel);
    const auto wanted = valid ? cell : juce::Point<int> (-1, -1);

    // The grid had a hover highlight and no cursor at all, so the one surface in
    // dew whose whole purpose is being clicked said nothing about it.
    setMouseCursor (valid ? cursor::clickable : cursor::idle);

    if (wanted != hoverCell)
    {
        const auto previous = hoverCell;
        hoverCell = wanted;

        // Two cells, not the whole grid: this fires on every pointer crossing.
        repaintCell (previous);
        repaintCell (hoverCell);
    }
}

void StepGridComponent::repaintCell (juce::Point<int> cell)
{
    if (cell.x < 0 || cell.y < 0)
        return;

    // Rounded OUT, then given the same margin it always had: a repaint region
    // has to cover the pixels the cell touches, and a cell's width is
    // fractional at most zooms.
    repaint (getBoundsForCell (cell.y, cell.x).getSmallestIntegerContainer().expanded (space::xxs));
}

void StepGridComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverCell.x >= 0)
    {
        const auto previous = hoverCell;
        hoverCell = { -1, -1 };
        repaintCell (previous);
    }
}

void StepGridComponent::applyPaint (const juce::MouseEvent& event)
{
    auto pattern = currentPattern();

    if (! pattern.isValid())
        return;

    const auto step = stepAtX (event.x);
    const auto row = rowAtY (event.y);

    if (step == lastPaintedStep && row == lastPaintedRow)
        return;

    const auto channel = channelForRow (row);

    if (! channel.isValid())
        return;

    // Checked again here, not only in mouseDown: a drag that began on a row
    // that takes notes can travel across one that does not, and the run-filling
    // below would paint right through it.
    if (! ProjectEdits::playsNotes (channel))
        return;

    // A drag reports a handful of positions per second, so at speed it jumps
    // several cells between samples. Fill the whole run along the row rather
    // than only where the pointer was reported, or a fast sweep leaves
    // survivors behind it. A change of row starts a new run.
    const auto continuing = row == lastPaintedRow && lastPaintedStep >= 0;
    const auto firstStep = continuing ? juce::jmin (lastPaintedStep, step) : step;
    const auto lastStep = continuing ? juce::jmax (lastPaintedStep, step) : step;

    lastPaintedStep = step;
    lastPaintedRow = row;

    const auto channelId = (int) channel[ids::id];
    auto& undo = document.getUndoManager();

    for (int s = firstStep; s <= lastStep; ++s)
    {
        const auto existing = ProjectEdits::findNoteAtStep (pattern, channelId, s);

        if (dragPaintsOn && ! existing.isValid())
        {
            ProjectEdits::addNote (pattern, channelId, s, 1, (int) channel[ids::basePitch],
                                   (float) editorState.getLastNoteVelocity(), &undo);
        }
        else if (! dragPaintsOn && existing.isValid())
        {
            ProjectEdits::removeNote (pattern, existing, &undo);
        }
    }

    repaint (0, 0, getWidth(), getRowsHeight());
}

void StepGridComponent::mouseDown (const juce::MouseEvent& event)
{
    // Same as the piano roll: wanting focus is not the same as having it, and
    // a click on the thing you are about to type at is when you meant to.
    grabKeyboardFocus();

    auto pattern = currentPattern();

    if (! pattern.isValid() || event.y >= getRowsHeight())
        return;

    const auto channel = channelForRow (rowAtY (event.y));

    if (! channel.isValid())
        return;

    // A waveform row is not a sequence of cells. Clicking it selects the
    // channel - that is what clicking a row means everywhere else - but it must
    // not write a note onto a channel that has no notes to play.
    if (! ProjectEdits::playsNotes (channel))
    {
        dragging = false;
        editorState.setSelectedChannelId ((int) channel[ids::id]);
        return;
    }

    // Right-drag and alt-drag always erase, whatever the first cell holds. This
    // had no modifier check at all, so a right-click behaved exactly like a
    // left one - which meant right-dragging from an empty cell ADDED steps,
    // the opposite of the piano roll and of what the gesture means anywhere.
    dragErasing = event.mods.isPopupMenu() || event.mods.isAltDown();

    // A left press NEVER removes. The first cell used to decide whether the
    // whole drag added or removed, so pressing a lit step turned the gesture
    // into an erase - which made the ordinary way of looking at a pattern,
    // clicking around it, delete the thing that was clicked. A left press on a
    // lit step selects its channel and leaves the step where it is; the run
    // filling below already skips cells that hold a note, so a drag across a
    // lit one steps over it rather than through it.
    dragPaintsOn = ! dragErasing;
    dragging = true;

    document.getUndoManager().beginNewTransaction (dragErasing    ? "Erase steps"
                                                   : dragPaintsOn ? "Add steps"
                                                                  : "Clear steps");

    // Once per gesture, not once per painted cell - a sweep across a row used
    // to re-select the same channel on every step it touched.
    editorState.setSelectedChannelId ((int) channel[ids::id]);

    lastPaintedStep = -1;
    lastPaintedRow = -1;
    applyPaint (event);
}

void StepGridComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging)
        applyPaint (event);
}

void StepGridComponent::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragErasing = false;
}

} // namespace dew
