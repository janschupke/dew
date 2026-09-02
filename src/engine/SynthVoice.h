#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "EngineSnapshot.h"

namespace dew
{

/** One oscillator plus an amplitude envelope.

    The oscillator is band-limited with PolyBLEP rather than generated naively.
    A naive saw or square is trivially cheaper, but at the pitches a bass or
    lead sits at it folds audible aliasing back down the spectrum and the result
    sounds broken rather than bright.

    The voice releases itself: a trigger says how many samples the note lasts,
    and the voice enters release when that runs out. Nothing else has to track
    note-offs across block or loop boundaries.
*/
class SynthVoice
{
public:
    void prepare (double sampleRate);

    void start (int pitch, float velocity, const OscSettings&, const AmpSettings&, int durationSamples);
    void release() noexcept;
    void reset() noexcept;

    bool isActive() const noexcept  { return active; }

    /** Age in samples since the note started - used for voice stealing. */
    juce::int64 getAge() const noexcept { return samplesSinceStart; }

    /** Adds this voice's mono output into `buffer`. */
    void renderAdd (float* buffer, int numSamples) noexcept;

private:
    float nextSample() noexcept;

    double currentSampleRate = 44100.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    // Triangle is produced by integrating the band-limited square, so it needs
    // to carry state between samples.
    double triangleState = 0.0;

    Waveform wave = Waveform::saw;
    float level = 1.0f;
    float oscGain = 0.8f;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    bool active = false;
    juce::int64 samplesSinceStart = 0;
    juce::int64 samplesUntilRelease = 0;
};

} // namespace dew
