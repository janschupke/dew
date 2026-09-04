// =============================================================================
// The step grid's painting.
//
// The same class, a second translation unit - the shape PianoRollPaint.cpp and
// PlaylistPaint.cpp already use.
//
// The cell grid, the beat and bar shading, an audio row's waveform, the
// beyond-the-end wash and the playhead. It reads a dozen of the grid's members
// and sets none of them, which is what makes it a file about drawing rather
// than a collaborator taking a dozen parameters.
// =============================================================================

#include "ui/StepGridComponent.h"

#include "engine/WaveformPeaks.h"
#include "io/SamplePool.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "ui/TimelinePaint.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewControls.h"

namespace dew
{

using namespace tokens;

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
        if (ProjectEdits::playsClips (channel))
        {
            paintWaveformRow (g, channel, rowBounds, colourValue, muted);
            continue;
        }

        for (int step = visible.getStart(); step < visible.getEnd(); ++step)
        {
            const auto cell = getBoundsForCell (row, step).reduced (2.0f, 4.0f);

            // Hover: show where a click would land, so an empty grid still
            // signals that it is interactive. The WHOLE cell lights, not the
            // inset the note fill uses - a 26px highlight inside a 34px row read
            // as a small block appearing rather than as this square being live.
            if (hoverCell.x == step && hoverCell.y == row)
            {
                const auto full = getBoundsForCell (row, step);

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
    paint::cursorOutline (g, getBoundsForCell (cursor.getPosition().y, cursor.getPosition().x),
                          cursor.isPlaced());
}
} // namespace dew
