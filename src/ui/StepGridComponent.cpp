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

void StepGridComponent::paintWaveformRow (juce::Graphics& g, const juce::ValueTree& channel,
                                          juce::Rectangle<int> rowBounds,
                                          juce::Colour channelColour, bool muted)
{
    auto* pool = samplePool;

    const auto sample = channel.getChildWithName (ids::SAMPLE);
    const auto path = sample.isValid() ? sample[ids::file].toString() : juce::String();

    if (pool == nullptr || path.isEmpty())
    {
        paint::emptyState (g, rowBounds, "No recording - arm this channel and press Record");
        return;
    }

    const auto& entry = pool->loadReference (path);

    if (! entry.isValid())
    {
        paint::emptyState (g, rowBounds, "Missing audio: " + path);
        return;
    }

    // Steps, not seconds: this grid is step-indexed like every other timeline in
    // the editor, so the sample is laid out over the steps it actually occupies
    // and lines up with the notes on the rows above it.
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto bpm = juce::jmax (1.0, (double) document.getState()[ids::tempoBpm]);
    const auto samplesPerStep = (60.0 / bpm) / (double) stepsPerBeat * entry.sourceSampleRate;

    const auto lengthSteps = samplesPerStep > 0.0
                                 ? (double) entry.audio->getNumSamples() / samplesPerStep
                                 : 0.0;

    if (lengthSteps <= 0.0)
        return;

    const auto startX = timeline.xForStep (0.0);
    const auto endX = timeline.xForStep (lengthSteps);

    if (endX <= startX)
        return;

    const auto trace = channelColour.withMultipliedAlpha (muted ? emphasis::subdued : 1.0f);

    paint::waveform (g, { (float) rowBounds.getY(), (float) rowBounds.getBottom() },
                     { startX, endX },
                     { juce::jmax (0.0f, startX), juce::jmin ((float) rowBounds.getRight(), endX) },
                     entry.peaks, [trace] (float) { return trace; });
}

void StepGridComponent::paint (juce::Graphics& g)
{
    const auto pattern = currentPattern();
    const auto steps = numSteps();
    const auto width = (float) timeline.pixelsPerStep;
    const auto visible = timeline.visibleStepRange ((float) getWidth(), steps);

    // The grid itself is drawn over the WHOLE width, so shading and bar lines
    // reach the edge of the panel even when the pattern is shorter than the
    // view. Notes still only exist inside `visible`.
    const auto painted = timeline.visibleStepRange ((float) getWidth());
    const auto meter = Meter::of (document.getState());
    const auto stepsPerBeat = meter.stepsPerBeat;
    const auto rows = getNumRows();
    const auto rowsHeight = getRowsHeight();

    // Anything below the last channel is not a control that stopped working.
    paint::inertArea (g, { 0, rowsHeight, getWidth(), juce::jmax (0, getHeight() - rowsHeight) });

    g.setColour (colour::well);
    g.fillRect (0, 0, getWidth(), rowsHeight);

    // --- beat and bar shading ------------------------------------------------
    for (int step = painted.getStart(); step < painted.getEnd(); ++step)
    {
        const auto beat = step / stepsPerBeat;

        // Counted WITHIN the bar, so the alternation restarts at every bar
        // line. Against the absolute beat index it would phase-flip each bar in
        // any odd meter, and a 3/4 grid visibly breathes.
        const auto beatInBar = beat % meter.beatsPerBar;
        const auto isBarStart = beatInBar == 0;

        if (beatInBar % 2 == 0)
        {
            g.setColour (isBarStart ? colour::barShade : colour::beatShade);
            g.fillRect (juce::Rectangle<float> (timeline.xForStep ((double) step), 0.0f, width,
                                                (float) rowsHeight));
        }
    }

    // --- cells ---------------------------------------------------------------
    for (int row = 0; row < rows; ++row)
    {
        const auto channel = channelForRow (row);
        const auto channelId = (int) channel[ids::id];
        const auto colourValue = entityColour::of (channel);

        const juce::Rectangle<int> rowBounds (0, row * size::rowHeight, getWidth(),
                                              size::rowHeight);

        if (channelId == editorState.getSelectedChannelId())
        {
            g.setColour (colour::accent.withAlpha (emphasis::tint));
            g.fillRect (rowBounds);
        }

        const auto muted = (bool) channel[ids::muted];

        // An audio channel has no steps to toggle: its content is one recording
        // laid along the same timeline, so the row shows the waveform instead.
        // Drawn over the WHOLE row rather than only the visible note range,
        // because a sample is continuous and a gap at the edge would read as
        // silence in the recording.
        if (ProjectEdits::isAudioChannel (channel))
        {
            paintWaveformRow (g, channel, rowBounds, colourValue, muted);
            continue;
        }

        for (int step = visible.getStart(); step < visible.getEnd(); ++step)
        {
            const auto cell = juce::Rectangle<float> (timeline.xForStep ((double) step),
                                                      (float) (row * size::rowHeight), width,
                                                      (float) size::rowHeight)
                                  .reduced (2.0f, 4.0f);

            // Hover: show where a click would land, so an empty grid still
            // signals that it is interactive. The WHOLE cell lights, not the
            // inset the note fill uses - a 26px highlight inside a 34px row read
            // as a small block appearing rather than as this square being live.
            if (hoverCell.x == step && hoverCell.y == row)
            {
                const auto full = juce::Rectangle<float> (timeline.xForStep ((double) step),
                                                          (float) (row * size::rowHeight), width,
                                                          (float) size::rowHeight);

                g.setColour (colour::surfaceRaised.withAlpha (emphasis::strong));
                g.fillRect (full.reduced (stroke::whisper));

                g.setColour (colour::accent.withAlpha (emphasis::subdued));
                g.drawRect (full.reduced (stroke::whisper), stroke::hairline);
            }

            const auto note = ProjectEdits::findNoteAtStep (pattern, channelId, step);

            if (! note.isValid())
                continue;

            // A note away from the channel's base pitch was written in the piano
            // roll; showing it differently stops the grid from implying the note
            // is something it is not.
            const auto atBasePitch = (int) note[ids::pitch] == (int) channel[ids::basePitch];
            const auto velocity = (float) juce::jlimit (0.2, 1.0, (double) note[ids::velocity]);

            auto fill = colourValue.withMultipliedAlpha (muted ? 0.35f : velocity);

            g.setColour (atBasePitch ? fill : emphasis::secondary (fill));
            g.fillRoundedRectangle (cell, radius::sm);

            if (! atBasePitch)
            {
                g.setColour (colourValue.withMultipliedAlpha (muted ? 0.4f : 1.0f));
                g.drawRoundedRectangle (cell, radius::sm, stroke::regular);
            }
        }
    }

    // --- grid lines ----------------------------------------------------------
    timelinePaint::verticalGrid (g, timeline, painted, meter.stepsPerBar(), meter.stepsPerBeat,
                                 0.0f, { 0.0f, (float) rowsHeight }, (float) getWidth());

    for (int row = 0; row <= rows; ++row)
    {
        g.setColour (colour::divider);
        g.drawHorizontalLine (row * size::rowHeight, 0.0f, (float) getWidth());
    }

    // Past the end of a short pattern is dimmed, not blanked - the rows and bar
    // lines carry on underneath, so the panel does not end in a void.
    const auto endX = timeline.xForStep ((double) steps);

    if (endX < (float) getWidth())
        paint::beyondEnd (g, { (int) endX, 0, getWidth() - (int) endX, rowsHeight }, endX);

    // --- position indicator --------------------------------------------------
    // Drawn while stopped too, dimmed. It used to vanish on stop, which made
    // "reset the position" indistinguishable from "lose the position", and
    // leaves nothing for a click on the ruler to move.
    if (engine.getMode() == Transport::Mode::pattern && rows > 0)
    {
        const auto playing = engine.isPlaying();
        const auto step = ((int) engine.getPlayheadSteps()) % steps;
        const auto x = timeline.xForStep ((double) step);

        playhead.set (playing);

        if (playing)
            timelinePaint::playheadColumn (g, { x, 0.0f, width, (float) rowsHeight });

        timelinePaint::playheadLine (g, x, { 0.0f, (float) rowsHeight }, playhead.brightness());
    }

    if (rows == 0)
    {
        paint::emptyState (g, getLocalBounds(), "No channels. Use + Channel to add one.");
    }
}

void StepGridComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto cell = juce::Point<int> (stepAtX (event.x), rowAtY (event.y));
    const auto channel = channelForRow (cell.y);

    // No cell highlight over a waveform: the hover exists to say "a click lands
    // here", and on an audio row it does not.
    const auto valid = event.y < getRowsHeight() && channel.isValid()
                       && ! ProjectEdits::isAudioChannel (channel);
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

    repaint (juce::Rectangle<int> ((int) timeline.xForStep ((double) cell.x),
                                   cell.y * size::rowHeight,
                                   (int) std::ceil (timeline.pixelsPerStep) + 2, size::rowHeight)
                 .expanded (space::xxs));
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

    // Checked again here, not only in mouseDown: a drag that began on a synth
    // row can travel across an audio one, and the run-filling below would paint
    // right through it.
    if (ProjectEdits::isAudioChannel (channel))
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

    // An audio row is a waveform, not a sequence of cells. Clicking it selects
    // the channel - that is what clicking a row means everywhere else - but it
    // must not write a note onto a channel that has no notes to play.
    if (ProjectEdits::isAudioChannel (channel))
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
