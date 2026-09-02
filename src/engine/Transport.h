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

    // --- the loop window -----------------------------------------------------
    /** The half-open window [start, end) the playhead wraps inside, in steps.

        A RANGE rather than a length, because a loop anchored at zero cannot say
        "bars five to nine" - and, worse, made the wrap point and the length of
        the material the same number, so there was nowhere to put a user's
        choice that did not also change what "the material" meant.

        `end <= start` is free-running, which is exactly what a length of zero
        meant before, so that case has not moved.

        Reversed arguments are NOT swapped. One rule here: backwards is no loop,
        like zero-width. Ordering the ends of a drag is a fact about a mouse and
        belongs where the mouse is.

        Transport does not know how long the material is and so does not clamp to
        it - that is AudioEngine's job, because only it holds a snapshot. Keeping
        it out of here is what lets a loop be tested without one.
    */
    void setLoopRange (double startSteps, double endSteps) noexcept;

    double getLoopStartSteps() const noexcept  { return loopStartSteps; }
    double getLoopEndSteps() const noexcept    { return loopEndSteps; }
    bool hasLoop() const noexcept              { return loopEndSteps > loopStartSteps; }

    /** The window in samples at the current tempo, rounded ONCE, here.

        Both ends are rounded from the same tempo in the same place, so advance(),
        a seek and the engine cannot disagree by a sample about where the loop
        ends - which at 6000 samples per step is inaudible once and a drifting
        flam after a thousand wraps.
    */
    juce::int64 loopStartSamples() const noexcept;
    juce::int64 loopEndSamples() const noexcept;

    /** Folds the position back into the window if it has run past the end.

        Public because it is the ONE wrap rule: advance() calls it, and so does
        anything that sets a position while a loop is set.
    */
    void wrapIntoLoop() noexcept;

    /** The same rule as arithmetic, over a window given in samples.

        Static and pure so the MESSAGE thread can predict it without a
        transport. It has to: a ruler click stores the raw position for instant
        feedback and the next audio block folds it, so a click landing outside
        the loop painted the clicked spot for a frame and then jumped - the
        seeker flickering. Predicting the fold makes the optimistic value the
        one the audio thread arrives at, rather than a different one.
    */
    static juce::int64 wrappedIntoLoop (juce::int64 positionSamples,
                                        juce::int64 startSamples,
                                        juce::int64 endSamples) noexcept;

    /** The old length-at-zero form, kept and not deprecated: most callers still
        mean precisely it - wrap at the end of the material, starting at the top.
    */
    void setLoopLengthSteps (int steps) noexcept  { setLoopRange (0.0, (double) juce::jmax (0, steps)); }

    void setMode (Mode m) noexcept   { mode = m; }
    Mode getMode() const noexcept    { return mode; }

    void start() noexcept            { playing = true; }
    void stop() noexcept             { playing = false; }
    bool isPlaying() const noexcept  { return playing; }

    void setPositionSamples (juce::int64 samples) noexcept  { positionSamples = juce::jmax ((juce::int64) 0, samples); }
    juce::int64 getPositionSamples() const noexcept         { return positionSamples; }

    void rewind() noexcept  { positionSamples = 0; }

    /** Advances by a block. Wraps into the loop window when one is set. */
    void advance (int numSamples) noexcept;

    /** Playhead in steps, as a fraction - for drawing a cursor. */
    double getPositionInSteps() const noexcept;

    static double samplesPerStepFor (double bpm, int stepsPerBeat, double sampleRate) noexcept;

private:
    double sampleRate = 44100.0;
    double tempoBpm = 128.0;
    int stepsPerBeat = 4;
    double loopStartSteps = 0.0;
    double loopEndSteps = 0.0;
    juce::int64 positionSamples = 0;
    bool playing = false;
    Mode mode = Mode::pattern;
};

} // namespace dew
