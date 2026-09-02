#include "StepGridComponent.h"

#include "../model/Ids.h"
#include "../model/ProjectEdits.h"
#include "DewLookAndFeel.h"

namespace dew
{

StepGridComponent::StepGridComponent (ProjectDocument& d, AudioEngine& e, EditorState& s)
    : document (d), engine (e), editorState (s)
{
    startTimerHz (30);
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

float StepGridComponent::stepWidth() const
{
    return (float) getWidth() / (float) numSteps();
}

int StepGridComponent::stepAtX (int x) const
{
    const auto width = stepWidth();
    return width > 0.0f ? juce::jlimit (0, numSteps() - 1, (int) ((float) x / width)) : 0;
}

int StepGridComponent::rowAtY (int y) const
{
    return y / rowHeight;
}

int StepGridComponent::getRequiredHeight() const
{
    int channels = 0;

    for (const auto& child : document.getState())
        if (child.hasType (ids::CHANNEL))
            ++channels;

    return juce::jmax (rowHeight, channels * rowHeight);
}

void StepGridComponent::timerCallback()
{
    const auto step = engine.getPlayheadSteps();

    // Only repaint when the playhead has moved to a different column.
    if ((int) step != (int) lastPlayheadStep)
    {
        lastPlayheadStep = step;
        repaint();
    }
}

void StepGridComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panelDark);

    const auto pattern = currentPattern();
    const auto steps = numSteps();
    const auto width = stepWidth();
    const auto stepsPerBeat = juce::jmax (1, (int) document.getState()[ids::stepsPerBeat]);

    // --- beat shading --------------------------------------------------------
    for (int step = 0; step < steps; ++step)
    {
        if ((step / stepsPerBeat) % 2 == 1)
            continue;

        g.setColour (Palette::beat);
        g.fillRect (juce::Rectangle<float> ((float) step * width, 0.0f,
                                            width * (float) stepsPerBeat, (float) getHeight()));
    }

    // --- cells ---------------------------------------------------------------
    int row = 0;

    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const auto channelId = (int) channel[ids::id];
        const auto colour = juce::Colour::fromString ("ff" + channel[ids::colour].toString().getLastCharacters (6));
        const auto rowBounds = juce::Rectangle<int> (0, row * rowHeight, getWidth(), rowHeight);

        if (channelId == editorState.getSelectedChannelId())
        {
            g.setColour (Palette::accent.withAlpha (0.07f));
            g.fillRect (rowBounds);
        }

        for (int step = 0; step < steps; ++step)
        {
            const auto note = ProjectEdits::findNoteAtStep (pattern, channelId, step);

            if (! note.isValid())
                continue;

            const auto cell = juce::Rectangle<float> ((float) step * width,
                                                      (float) (row * rowHeight),
                                                      width, (float) rowHeight).reduced (1.5f);

            // A note away from the channel's base pitch was written in the piano
            // roll; showing it differently stops the grid from implying the
            // note is something it is not.
            const auto atBasePitch = (int) note[ids::pitch] == (int) channel[ids::basePitch];

            g.setColour (atBasePitch ? colour : colour.withSaturation (0.35f));
            g.fillRoundedRectangle (cell, 3.0f);

            if (! atBasePitch)
            {
                g.setColour (colour);
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
        }

        ++row;
    }

    // --- grid lines ----------------------------------------------------------
    for (int step = 0; step <= steps; ++step)
    {
        const auto x = (float) step * width;
        g.setColour (step % stepsPerBeat == 0 ? Palette::lineStrong : Palette::line);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    }

    for (int r = 0; r <= row; ++r)
    {
        g.setColour (Palette::line);
        g.drawHorizontalLine (r * rowHeight, 0.0f, (float) getWidth());
    }

    // --- playhead ------------------------------------------------------------
    if (engine.isPlaying() && engine.getMode() == Transport::Mode::pattern)
    {
        const auto step = (int) engine.getPlayheadSteps() % steps;
        g.setColour (Palette::playhead.withAlpha (0.28f));
        g.fillRect (juce::Rectangle<float> ((float) step * width, 0.0f, width, (float) getHeight()));
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

    lastPaintedStep = step;
    lastPaintedRow = row;

    int index = 0;

    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if (index++ != row)
            continue;

        const auto channelId = (int) channel[ids::id];
        const auto existing = ProjectEdits::findNoteAtStep (pattern, channelId, step);

        if (dragPaintsOn && ! existing.isValid())
        {
            ProjectEdits::addNote (pattern, channelId, step, 1,
                                   (int) channel[ids::basePitch], 1.0f,
                                   &document.getUndoManager());
        }
        else if (! dragPaintsOn && existing.isValid())
        {
            ProjectEdits::removeNote (pattern, existing, &document.getUndoManager());
        }

        editorState.setSelectedChannelId (channelId);
        repaint();
        return;
    }
}

void StepGridComponent::mouseDown (const juce::MouseEvent& event)
{
    auto pattern = currentPattern();

    if (! pattern.isValid())
        return;

    const auto step = stepAtX (event.x);
    const auto row = rowAtY (event.y);

    int index = 0;

    for (const auto& channel : document.getState())
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        if (index++ != row)
            continue;

        // The first cell decides whether the whole drag adds or removes.
        dragPaintsOn = ! ProjectEdits::findNoteAtStep (pattern, (int) channel[ids::id], step).isValid();
        break;
    }

    document.getUndoManager().beginNewTransaction (dragPaintsOn ? "Add steps" : "Clear steps");

    lastPaintedStep = -1;
    lastPaintedRow = -1;
    applyPaint (event);
}

void StepGridComponent::mouseDrag (const juce::MouseEvent& event)
{
    applyPaint (event);
}

} // namespace dew
