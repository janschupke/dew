#include "StepGridComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "design/Tokens.h"
#include "primitives/DewControls.h"

namespace dew
{

using namespace tokens;

StepGridComponent::StepGridComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    setComponentID ("stepGrid");
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

void StepGridComponent::updateZoom()
{
    const auto steps = numSteps();
    const auto width = (float) getWidth();

    if (width <= 0.0f)
        return;

    // Fill the width when the pattern can, and fall back to scrolling when a
    // step would otherwise be too narrow to aim at. A 16-step pattern fits; a
    // 128-step one scrolls at a workable cell size instead of becoming hairlines.
    timeline.pixelsPerStep = juce::jlimit ((double) minCellWidth, (double) maxCellWidth,
                                           (double) width / (double) steps);

    const auto scrollable = isScrollable();
    horizontalScroll.setVisible (scrollable);

    if (! scrollable)
        timeline.scrollOffsetSteps = 0.0;

    timeline.clampScroll (width, steps);

    const juce::ScopedValueSetter<bool> quiet (updatingScrollBar, true);
    horizontalScroll.setRangeLimits (0.0, (double) steps, juce::dontSendNotification);
    horizontalScroll.setCurrentRange (timeline.scrollOffsetSteps,
                                      timeline.visibleSteps (width), juce::dontSendNotification);
}

bool StepGridComponent::isScrollable() const
{
    return timeline.visibleSteps ((float) getWidth()) < (double) numSteps() - 1e-9;
}

void StepGridComponent::resized()
{
    horizontalScroll.setBounds (0, getHeight() - scrollThickness, getWidth(), scrollThickness);
    updateZoom();
}

void StepGridComponent::scrollBarMoved (juce::ScrollBar*, double start)
{
    if (updatingScrollBar)
        return;

    timeline.scrollOffsetSteps = start;
    repaint();
}

void StepGridComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! isScrollable())
        return;

    timeline.scrollOffsetSteps -= (double) (wheel.deltaX + wheel.deltaY) * 6.0;
    updateZoom();
    repaint();
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

    if (step != lastPlayheadStep)
    {
        lastPlayheadStep = step;

        if (engine.isPlaying())
            repaint (0, 0, getWidth(), getRowsHeight());
    }
}

void StepGridComponent::paint (juce::Graphics& g)
{
    const auto pattern = currentPattern();
    const auto steps = numSteps();
    const auto width = (float) timeline.pixelsPerStep;
    const auto visible = timeline.visibleStepRange ((float) getWidth(), steps);
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);
    const auto rows = getNumRows();
    const auto rowsHeight = getRowsHeight();

    // Anything below the last channel is not a control that stopped working.
    paint::inertArea (g, { 0, rowsHeight, getWidth(), juce::jmax (0, getHeight() - rowsHeight) });

    g.setColour (colour::well);
    g.fillRect (0, 0, getWidth(), rowsHeight);

    // --- beat and bar shading ------------------------------------------------
    for (int step = visible.getStart(); step < visible.getEnd(); ++step)
    {
        const auto beat = step / stepsPerBeat;
        const auto isBarStart = beat % 4 == 0;

        if (beat % 2 == 0 || isBarStart)
        {
            g.setColour (isBarStart ? colour::barShade : colour::beatShade);
            g.fillRect (juce::Rectangle<float> (timeline.xForStep ((double) step), 0.0f,
                                                width, (float) rowsHeight));
        }
    }

    // --- cells ---------------------------------------------------------------
    for (int row = 0; row < rows; ++row)
    {
        const auto channel = channelForRow (row);
        const auto channelId = (int) channel[ids::id];
        const auto colourValue = juce::Colour::fromString (
            "ff" + channel[ids::colour].toString().getLastCharacters (6));

        const juce::Rectangle<int> rowBounds (0, row * size::rowHeight, getWidth(), size::rowHeight);

        if (channelId == editorState.getSelectedChannelId())
        {
            g.setColour (colour::accent.withAlpha (0.08f));
            g.fillRect (rowBounds);
        }

        const auto muted = (bool) channel[ids::muted];

        for (int step = visible.getStart(); step < visible.getEnd(); ++step)
        {
            const auto cell = juce::Rectangle<float> (timeline.xForStep ((double) step),
                                                      (float) (row * size::rowHeight),
                                                      width, (float) size::rowHeight)
                                  .reduced (2.0f, 4.0f);

            // Hover: show where a click would land, so an empty grid still
            // signals that it is interactive.
            if (hoverCell.x == step && hoverCell.y == row)
            {
                g.setColour (colour::surfaceRaised.withAlpha (0.75f));
                g.fillRoundedRectangle (cell, radius::sm);
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

            g.setColour (atBasePitch ? fill : fill.withSaturation (0.3f));
            g.fillRoundedRectangle (cell, radius::sm);

            if (! atBasePitch)
            {
                g.setColour (colourValue.withMultipliedAlpha (muted ? 0.4f : 1.0f));
                g.drawRoundedRectangle (cell, radius::sm, stroke::regular);
            }
        }
    }

    // --- grid lines ----------------------------------------------------------
    for (int step = visible.getStart(); step <= visible.getEnd(); ++step)
    {
        const auto x = timeline.xForStep ((double) step);
        const auto isBarLine = (step % (stepsPerBeat * 4)) == 0;

        g.setColour (isBarLine ? colour::dividerStrong : colour::divider);
        g.drawVerticalLine ((int) x, 0.0f, (float) rowsHeight);
    }

    for (int row = 0; row <= rows; ++row)
    {
        g.setColour (colour::divider);
        g.drawHorizontalLine (row * size::rowHeight, 0.0f, (float) getWidth());
    }

    // Past the end of a short pattern is not part of the pattern.
    const auto endX = timeline.xForStep ((double) steps);

    if (endX < (float) getWidth())
        paint::inertArea (g, { (int) endX, 0, getWidth() - (int) endX, rowsHeight });

    // --- playhead ------------------------------------------------------------
    if (engine.isPlaying() && engine.getMode() == Transport::Mode::pattern && rows > 0)
    {
        const auto step = ((int) engine.getPlayheadSteps()) % steps;
        const auto x = timeline.xForStep ((double) step);

        g.setColour (colour::playhead.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, width, (float) rowsHeight));

        g.setColour (colour::playhead);
        g.drawVerticalLine ((int) x, 0.0f, (float) rowsHeight);
    }

    if (rows == 0)
    {
        g.setColour (colour::textSecondary);
        g.setFont (type::font (type::body));
        g.drawText ("No channels. Use + Channel to add one.", getLocalBounds(),
                    juce::Justification::centred, false);
    }
}

void StepGridComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto cell = juce::Point<int> (stepAtX (event.x), rowAtY (event.y));
    const auto valid = event.y < getRowsHeight() && channelForRow (cell.y).isValid();
    const auto wanted = valid ? cell : juce::Point<int> (-1, -1);

    if (wanted != hoverCell)
    {
        hoverCell = wanted;
        repaint (0, 0, getWidth(), getRowsHeight());
    }
}

void StepGridComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverCell.x >= 0)
    {
        hoverCell = { -1, -1 };
        repaint (0, 0, getWidth(), getRowsHeight());
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

    lastPaintedStep = step;
    lastPaintedRow = row;

    const auto channelId = (int) channel[ids::id];
    const auto existing = ProjectEdits::findNoteAtStep (pattern, channelId, step);

    if (dragPaintsOn && ! existing.isValid())
    {
        ProjectEdits::addNote (pattern, channelId, step, 1,
                               (int) channel[ids::basePitch],
                               (float) editorState.getLastNoteVelocity(),
                               &document.getUndoManager());
    }
    else if (! dragPaintsOn && existing.isValid())
    {
        ProjectEdits::removeNote (pattern, existing, &document.getUndoManager());
    }

    editorState.setSelectedChannelId (channelId);
    repaint (0, 0, getWidth(), getRowsHeight());
}

void StepGridComponent::mouseDown (const juce::MouseEvent& event)
{
    auto pattern = currentPattern();

    if (! pattern.isValid() || event.y >= getRowsHeight())
        return;

    const auto channel = channelForRow (rowAtY (event.y));

    if (! channel.isValid())
        return;

    // The first cell decides whether the whole drag adds or removes.
    dragPaintsOn = ! ProjectEdits::findNoteAtStep (pattern, (int) channel[ids::id],
                                                   stepAtX (event.x)).isValid();
    dragging = true;

    document.getUndoManager().beginNewTransaction (dragPaintsOn ? "Add steps" : "Clear steps");

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
}

} // namespace dew
