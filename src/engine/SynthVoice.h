#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

#include "engine/EngineSnapshot.h"
#include "engine/Wavetable.h"
#include "model/Constants.h"

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

    /** Starts a note, optionally `startOffset` samples into the coming block.

        The offset is why a synth note and an audio clip agree about where a bar
        is. Sequencer::collect has always computed which sample of the block a
        step falls on, and the engine has always thrown it away - so every synth
        note started at sample 0 of its block while SamplePlayer was
        sample-accurate, and a render at one block size did not match a render
        at another.
    */
    void start (int pitch, float velocity, const OscBankSnapshot&, const AmpSettings&,
                int durationSamples, int startOffset = 0);
    void release() noexcept;
    void reset() noexcept;

    bool isActive() const noexcept  { return active; }

    /** The pitch this voice is sounding, so a held note can be released by name
        rather than by index. -1 when idle.
    */
    int getPitch() const noexcept { return active ? currentPitch : -1; }

    /** Age in samples since the note started - used for voice stealing. */
    juce::int64 getAge() const noexcept { return samplesSinceStart; }

    /** Pushes the CURRENT wavetable positions from a channel's bank into the
        voices already sounding.

        The one oscillator setting that is not latched at note-on, and
        deliberately so: a wavetable whose position can only change between
        notes is a wavetable that never moves, which is the whole reason to have
        one. Everything else still latches - turning the detune knob changes the
        next note, not this one.

        Costs nothing when nothing is automated: the bank still holds what
        note-on read, so each slot is assigned its own value back and the render
        is bit-identical to one with no automation at all.
    */
    void setWavetablePosition (const OscBankSnapshot&) noexcept;

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

    /** One wavetable oscillator, with its unison stack inside it.

        A separate struct rather than a fifth case in Oscillator::nextSample().
        Two reasons, both load-bearing: the classic switch above stays byte for
        byte what it was, which pinned renders depend on; and seven phases of
        unison state would otherwise be carried by every classic slot that will
        never use them.
    */
    struct WavetableOscillator
    {
        const Wavetable* table = nullptr;

        std::array<double, kMaxUnisonVoices> phase {};
        std::array<double, kMaxUnisonVoices> phaseIncrement {};

        /** The increments latched at note-on, before any bend - the same
            reasoning as Oscillator::baseIncrement.
        */
        std::array<double, kMaxUnisonVoices> baseIncrement {};

        int numUnison = 1;

        /** Which bank slot this came from, so a live position can be pushed
            back into a voice whose enabled slots were compacted at note-on.
        */
        int slot = -1;

        /** Which band-limited copy of the table to read. Recomputed per block
            with the increments, never per sample.
        */
        int mip = 0;

        /** Already divided by sqrt(numUnison) - see start(). */
        float gain = 0.8f;

        float basePosition = 0.0f;
        float positionMod = 0.0f;
        PositionSource source = PositionSource::envelope;

        double lfoPhase = 0.0;
        double lfoIncrement = 0.0;

        /** `envelope` is the amplitude envelope's value for this sample, which
            doubles as the modulation source when the slot asks for it - so an
            envelope-driven position needs no second envelope's parameters.
        */
        float nextSample (float envelope) noexcept;

        void updateMip() noexcept;
    };

    double currentSampleRate = kDefaultSampleRate;

    std::array<Oscillator, kMaxOscillators> oscillators;
    std::array<WavetableOscillator, kMaxOscillators> wavetables;

    /** How many of `oscillators` this note is actually running. Switched-off
        slots are skipped once at note-on rather than tested every sample.
    */
    int numOscillators = 0;

    /** How many of `wavetables` this note is running. Zero on a voice whose
        slots are all classic, which is what keeps that path untouched.
    */
    int numWavetables = 0;

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

    /** Samples still to wait before this voice sounds.

        While it counts down the voice is ACTIVE - it holds its slot and answers
        to its pitch - but produces nothing, advances no envelope, and does not
        age. Not ageing is the subtle part: getAge drives voice stealing, and a
        voice delayed by half a block that counted those samples would look
        older than one that started at the top of the block and get stolen
        first.
    */
    juce::int64 samplesUntilStart = 0;
};

} // namespace dew
