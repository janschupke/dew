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

    /** Bends this voice, in semitones, and applies vibrato of `modulation`
        depth (0..1). Called once per block rather than per sample: at a 256
        sample block that is a 172 Hz update, some thirty steps per cycle of the
        vibrato, which is smooth to the ear and leaves the sample loop alone.

        Zero for both restores the exact increments latched at note-on, so an
        untouched controller is bit-for-bit what the engine rendered before
        there was one - which is what lets the offline renderer's output be
        pinned unchanged.

        `numSamples` is how far to advance the vibrato LFO.
    */
    void setPitchModulation (float bendSemitones, float modulation, int numSamples) noexcept;

    /** Adds this voice's mono output into `buffer`. */
    void renderAdd (float* buffer, int numSamples) noexcept;

    /** A gentle vibrato, not a siren: the mod wheel fully up is a 50 cent
        sweep, which is about what a player expects from it.
    */
    static constexpr float maxVibratoSemitones = 0.5f;
    static constexpr float vibratoHz = 5.5f;

private:
    /** One band-limited oscillator's state.

        Latched at note-on like everything else about a voice: turning a knob
        changes the next note, not the one already sounding.
    */
    struct Oscillator
    {
        double phase = 0.0;
        double phaseIncrement = 0.0;

        /** The increment latched at note-on, before any bend. Kept so that
            modulation is always computed from the note's own pitch rather than
            from the last bent value, which would drift as the wheel moved.
        */
        double baseIncrement = 0.0;

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

    /** Where the vibrato LFO has got to, in cycles. Advanced per block by
        setPitchModulation, and reset whenever modulation returns to nothing so
        a later note does not inherit a stale phase.
    */
    double vibratoPhase = 0.0;

    /** Whether the increments currently differ from `baseIncrement`. Lets the
        unmodulated case restore them exactly once and then do nothing at all.
    */
    bool modulated = false;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    bool active = false;
    int currentPitch = -1;
    juce::int64 samplesSinceStart = 0;
    juce::int64 samplesUntilRelease = 0;
};

} // namespace dew
