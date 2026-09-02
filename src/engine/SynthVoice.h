#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

#include "EngineSnapshot.h"

namespace dew
{

/** Up to kMaxOscillators oscillators sharing one amplitude envelope.

    The oscillators are band-limited with PolyBLEP rather than generated
    naively. A naive saw or square is trivially cheaper, but at the pitches a
    bass or lead sits at it folds audible aliasing back down the spectrum and
    the result sounds broken rather than bright.

    The voice releases itself: a trigger says how many samples the note lasts,
    and the voice enters release when that runs out. Nothing else has to track
    note-offs across block or loop boundaries.
*/
class SynthVoice
{
public:
    void prepare (double sampleRate);

    void start (int pitch, float velocity, const OscBankSnapshot&, const AmpSettings&,
                int durationSamples);
    void release() noexcept;
    void reset() noexcept;

    bool isActive() const noexcept  { return active; }

    /** The pitch this voice is sounding, so a held note can be released by name
        rather than by index. -1 when idle.
    */
    int getPitch() const noexcept { return active ? currentPitch : -1; }

    /** Age in samples since the note started - used for voice stealing. */
    juce::int64 getAge() const noexcept { return samplesSinceStart; }

    /** Adds this voice's mono output into `buffer`. */
    void renderAdd (float* buffer, int numSamples) noexcept;

private:
    /** One band-limited oscillator's state.

        Latched at note-on like everything else about a voice: turning a knob
        changes the next note, not the one already sounding.
    */
    struct Oscillator
    {
        double phase = 0.0;
        double phaseIncrement = 0.0;

        // Triangle is produced by integrating the band-limited square, so it
        // needs to carry state between samples - per oscillator, not per voice.
        double triangleState = 0.0;

        Waveform wave = Waveform::saw;
        float gain = 0.8f;

        float nextSample() noexcept;
    };

    double currentSampleRate = 44100.0;

    std::array<Oscillator, kMaxOscillators> oscillators;

    /** How many of `oscillators` this note is actually running. Switched-off
        slots are skipped once at note-on rather than tested every sample.
    */
    int numOscillators = 0;

    float level = 1.0f;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    bool active = false;
    int currentPitch = -1;
    juce::int64 samplesSinceStart = 0;
    juce::int64 samplesUntilRelease = 0;
};

} // namespace dew
