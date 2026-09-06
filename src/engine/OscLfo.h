#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "model/InstrumentType.h"

namespace dew
{

/** One oscillator slot's low-frequency oscillator, as a voice holds it.

    Deliberately NOT a fifth case in SynthVoice::Oscillator, and not built on
    it either. Two reasons, both load-bearing:

    - The shapes here are NAIVE, where the audio oscillator's are band-limited.
      PolyBLEP exists to stop a saw folding aliasing back down the spectrum, and
      at five hertz there is nothing to fold; worse, it would round off exactly
      the hard edge that makes a square LFO a trill rather than a wobble. A
      control signal wants the corner it asks for.
    - The audio oscillator's switch stays byte for byte what it was, which the
      pinned renders depend on.

    Which slot each one drives is carried here rather than looked up, because
    the voice compacts its enabled slots at note-on and the LFO has to find the
    oscillator it was paired with afterwards.
*/
struct OscLfo
{
    double phase = 0.0;
    double increment = 0.0;

    Waveform wave = Waveform::sine;

    float toPitch = 0.0f;  ///< semitones, either way
    float toVolume = 0.0f; ///< -1..1, scaling the level
    float toPan = 0.0f;    ///< -1..1, sweeping across the pair

    /** Where the oscillator this drives ended up, once the voice had compacted
        its enabled slots. Exactly one of the two is set. */
    int classicIndex = -1;
    int wavetableIndex = -1;

    /** The value the LFO is holding, in -1..1.

        Read without advancing, which is what lets the per-block pitch update
        and the per-sample level and pan share one phase: the pitch fold reads
        what the previous block left, and the sample loop advances it. A second
        accumulator would be a second thing to drift.
    */
    float value() const noexcept
    {
        switch (wave)
        {
            case Waveform::sine:
                return (float) std::sin (juce::MathConstants<double>::twoPi * phase);

            case Waveform::saw: return (float) (2.0 * phase - 1.0);

            case Waveform::square: return phase < 0.5 ? 1.0f : -1.0f;

            case Waveform::triangle: return (float) (4.0 * std::abs (phase - 0.5) - 1.0);
        }

        return 0.0f;
    }

    void advance() noexcept
    {
        phase += increment;

        if (phase >= 1.0)
            phase -= 1.0;
    }
};

} // namespace dew
