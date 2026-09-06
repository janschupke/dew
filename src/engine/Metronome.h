#pragma once

#include <juce_core/juce_core.h>

#include "engine/Transport.h"
#include "model/Constants.h"
#include "model/Meter.h"

namespace dew
{

/** The click, as one voice.

    A short decaying sine burst and nothing else. It knows about a sample rate
    and about being struck; it knows nothing about a transport, a snapshot or a
    bar, which is the same split Sequencer and AudioEngine already have -
    Sequencer works out WHAT falls inside a block and the engine drives it.

    One voice rather than a pool, and that is deliberate: two clicks a beat
    apart cannot overlap at any tempo dew renders, and a strike that lands while
    the last one is still ringing is a tempo nobody would want to hear the tail
    of anyway. AudioEngine renders a block in SEGMENTS around the strikes, so
    several clicks in one buffer need no queue and have no drop policy.

    Everything the audio thread calls only writes scalars. The one std::pow is
    in prepare(), on the message thread, where arithmetic is allowed to be
    expensive.
*/
class Metronome
{
public:
    /** The two pitches, and how loud each is.

        A fifth apart rather than an octave: an octave reads as the same note
        twice and the accent is meant to be TELLABLE at a glance, under a mix,
        without being louder enough to hurt.
    */
    static constexpr double beatHz = 1000.0;
    static constexpr double accentHz = 1500.0;
    static constexpr float beatGain = 0.22f;
    static constexpr float accentGain = 0.34f;

    /** Short enough that a click at 999 bpm is over before the next one, long
        enough to have a pitch rather than being a tick. */
    static constexpr double clickSeconds = 0.030;

    void prepare (double sampleRate) noexcept;

    /** Silences the voice at once. Not a fade: this is called when the click is
        switched off or the engine released, and a tail nobody asked for is
        worse than a discontinuity nobody hears at that level. */
    void reset() noexcept;

    /** Restarts the voice. `accent` picks the bar pitch and gain. */
    void strike (bool accent) noexcept;

    /** ADDS `numSamples` of the voice to both sides. Adds nothing, and costs a
        return, when the voice is silent. */
    void render (float* left, float* right, int numSamples) noexcept;

    bool isSounding() const noexcept
    {
        return remaining > 0;
    }

private:
    double sampleRate = kDefaultSampleRate;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float gain = 0.0f;

    /** Per-sample multiplier, precomputed from clickSeconds in prepare(). */
    float decay = 1.0f;

    int clickSamples = 0;
    int remaining = 0;
};

/** How long a count-in of `bars` is, in samples.

    A free function over THE conversion rather than arithmetic at the call site,
    because the call site got it wrong once already: MainComponent::finishRecording
    read `240.0 / bpm` with the four beats folded into the constant, which made
    every take a quarter too long in 3/4.

    `stepsPerBeat` and `beatsPerBar` only. `beatUnit` is the notational
    denominator and is inert in the engine - see Meter - so it must never reach
    Transport::samplesPerStepFor.
*/
juce::int64 countInSamplesFor (int bars, const Meter& meter, double bpm,
                               double sampleRate) noexcept;

} // namespace dew
