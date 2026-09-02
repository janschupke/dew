#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** Converts between musical time and samples, and holds the playhead.

    All timing is derived from a sample count, never accumulated in floating
    point per block, so the playhead cannot drift over a long render.
*/
class Transport
{
public:
    enum class Mode { pattern, song };

    void prepare (double newSampleRate);

    void setTempo (double bpm, int stepsPerBeat);
    double getTempo() const noexcept        { return tempoBpm; }
    int getStepsPerBeat() const noexcept    { return stepsPerBeat; }

    /** Samples per sequencer step. A step is a 1/stepsPerBeat note. */
    double samplesPerStep() const noexcept;

    /** Length of one loop in steps, or 0 for free-running. */
    void setLoopLengthSteps (int steps) noexcept  { loopLengthSteps = juce::jmax (0, steps); }
    int getLoopLengthSteps() const noexcept       { return loopLengthSteps; }

    void setMode (Mode m) noexcept   { mode = m; }
    Mode getMode() const noexcept    { return mode; }

    void start() noexcept            { playing = true; }
    void stop() noexcept             { playing = false; }
    bool isPlaying() const noexcept  { return playing; }

    void setPositionSamples (juce::int64 samples) noexcept  { positionSamples = juce::jmax ((juce::int64) 0, samples); }
    juce::int64 getPositionSamples() const noexcept         { return positionSamples; }

    void rewind() noexcept  { positionSamples = 0; }

    /** Advances by a block. Wraps at the loop point when one is set. */
    void advance (int numSamples) noexcept;

    /** Playhead in steps, as a fraction - for drawing a cursor. */
    double getPositionInSteps() const noexcept;

    static double samplesPerStepFor (double bpm, int stepsPerBeat, double sampleRate) noexcept;

private:
    double sampleRate = 44100.0;
    double tempoBpm = 128.0;
    int stepsPerBeat = 4;
    int loopLengthSteps = 0;
    juce::int64 positionSamples = 0;
    bool playing = false;
    Mode mode = Mode::pattern;
};

} // namespace dew
