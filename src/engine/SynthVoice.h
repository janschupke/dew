#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

#include "engine/EngineSnapshot.h"
#include "engine/OscLfo.h"
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

    bool isActive() const noexcept
    {
        return active;
    }

    /** The pitch this voice is sounding, so a held note can be released by name
        rather than by index. -1 when idle.
    */
    int getPitch() const noexcept
    {
        return active ? currentPitch : -1;
    }

    /** Age in samples since the note started - used for voice stealing. */
    juce::int64 getAge() const noexcept
    {
        return samplesSinceStart;
    }

    /** Pushes the CURRENT wavetable positions from a channel's bank into the
        voices already sounding.

        The first of three oscillator settings that are not latched at note-on,
        and deliberately so: a wavetable whose position can only change between
        notes is a wavetable that never moves, which is the whole reason to have
        one. The other two are the FM matrix and the LFO, below. Everything else
        still latches - turning the detune knob changes the next note, not this
        one.

        Costs nothing when nothing is automated: the bank still holds what
        note-on read, so each slot is assigned its own value back and the render
        is bit-identical to one with no automation at all.
    */
    void setWavetablePosition (const OscBankSnapshot&) noexcept;

    /** Pushes the CURRENT FM matrix from a channel's bank into the voices
        already sounding.

        The second of the three, and for the reason the wavetable position is
        the first: an FM index that can only change between notes is an FM index
        that never moves, and a swept index is most of what anybody wants a
        matrix for.

        Does nothing to a voice that started with an empty matrix. Such a voice
        is running the plain render path, which has no phase offsets and no
        output column to apply, and half-converting it mid-note would be a click
        rather than a sweep - so a matrix switched on reaches the NEXT note.

        Costs nothing when nothing is automated: the bank still holds what
        note-on read, so each cell is assigned its own value back.
    */
    void setFmMatrix (const OscBankSnapshot&) noexcept;

    /** Pushes the CURRENT LFO settings from a channel's bank into the voices
        already sounding.

        The third setting that is not latched at note-on. An LFO whose rate and
        depth can only change between notes is an LFO nobody can dial in: every
        adjustment is inaudible until the next note starts, which reads as the
        control doing nothing at all.

        The PHASE is not touched. Leaving it is what makes a rate change a sweep
        rather than a retrigger, and a retrigger part-way through a note is a
        click - which is also why startLfo zeroes it and this does not.

        Does nothing to an oscillator that started with its LFO inactive. Such a
        slot is in the plain half of the note-on partition, has no OscLfo paired
        with it, and cannot join the other half without re-ordering a float sum
        that the pinned renders depend on - so an LFO switched ON reaches the
        NEXT note, exactly as a matrix switched on does. Depth, rate and wave
        reach the one sounding.

        May WIDEN `panned` but never narrows it: raising pan depth mid-note has
        to reach the pan pair, and dropping a sounding voice back onto the mono
        path would be a click. The pair is decided one block ahead of this, so a
        depth raised from zero starts sweeping on the block after the one that
        raised it.

        Costs nothing when nothing is automated: the bank still holds what
        note-on read, so each field is assigned its own value back.
    */
    void setLfo (const OscBankSnapshot&) noexcept;

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

    /** Adds this voice's output into `mono`, and any oscillator the LFO is
        sweeping across the field into `panLeft` / `panRight` instead.

        The pan pair is OPTIONAL and defaults to nothing, which is what keeps
        every caller that predates LFOs - four test files that pin this engine
        sample for sample among them - compiling and rendering unchanged. Pass
        null and a panned oscillator still moves in pitch and level; it just has
        nowhere to put a side, so it sums into the mono buffer like the rest.

        Both are ADDED to, never written, exactly as the mono form always was.
    */
    void renderAdd (float* mono, int numSamples, float* panLeft = nullptr,
                    float* panRight = nullptr) noexcept;

    /** Whether this voice has an oscillator whose LFO is sweeping it across the
        field, so the caller knows whether to hand renderAdd a pan pair at all.

        A fact about what the voice LATCHED, not about the document: the answer
        has to survive the bank changing under a note that is already sounding.
    */
    bool isPanned() const noexcept
    {
        return panned;
    }

    /** A gentle vibrato, not a siren: the mod wheel fully up is a 50 cent
        sweep, which is about what a player expects from it.
    */
    static constexpr float maxVibratoSemitones = 0.5f;
    static constexpr float vibratoHz = 5.5f;

    /** How far a full-amplitude modulator at a full amount displaces its
        target's phase, in whole CYCLES - dew's phase is 0..1 rather than
        radians, so two cycles is a modulation index of about 12.6.

        Chosen to be the top of the useful range rather than the top of the
        possible one: past this the sidebands are dense enough that turning the
        knob further changes which noise you get rather than how bright it is.
    */
    static constexpr double maxFmPhaseOffset = 2.0;

private:
    /** One band-limited oscillator's state.

        Latched at note-on like almost everything else about a voice: turning a
        knob changes the next note, not the one already sounding. The three
        exceptions are named above - the wavetable position, the FM matrix and
        the LFO.
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

        /** Which bank slot this came from. The wavetable form has carried one
            since live positions were pushed back into sounding voices; this one
            needs it for the same shape of reason - note-on PARTITIONS the run,
            so an index into this array says nothing about which row of the FM
            matrix an oscillator is.
        */
        int slot = -1;

        /** @param phaseOffset  in cycles, added to the phase this sample READS
                                without disturbing the one it accumulates.

            Zero is not merely equal to no offset, it is untouched: the guard
            inside skips the arithmetic entirely, so the plain render path is
            the same expressions in the same order it has always been.
        */
        float nextSample (double phaseOffset = 0.0) noexcept;
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
        float nextSample (float envelope, double phaseOffset = 0.0) noexcept;

        void updateMip() noexcept;
    };

    double currentSampleRate = kDefaultSampleRate;

    std::array<Oscillator, kMaxOscillators> oscillators;
    std::array<WavetableOscillator, kMaxOscillators> wavetables;

    /** Each array is PARTITIONED at note-on: the oscillators with no LFO first,
        then the ones that have one.

        `oscillators[0, numMonoOscillators)` is the plain pass, and it holds
        exactly the oscillators it always held, in the order it always held
        them, on any voice with no LFO anywhere - which is what the pinned
        renders are pinned to. Summing is not associative, so a slot that merely
        MOVED within the run would change the last bits of every note.

        Named `mono` rather than counted from the top for the same reason the
        wavetable pass is counted separately: the two runs are different passes
        with different arithmetic, and a single `numOscillators` that quietly
        came to mean one of them is a loop somebody forgets to widen. Renaming
        made every such loop a compile error rather than a silence - see
        setPitchModulation, which would otherwise have stopped bending half the
        voice.
    */
    int numMonoOscillators = 0;
    int numLfoOscillators = 0;
    int numMonoWavetables = 0;
    int numLfoWavetables = 0;

    int totalOscillators() const noexcept
    {
        return numMonoOscillators + numLfoOscillators;
    }

    int totalWavetables() const noexcept
    {
        return numMonoWavetables + numLfoWavetables;
    }

    /** One per oscillator this note is running that has an LFO switched on and
        asking for something. Zero on every voice that has none, which is what
        keeps the third pass costing nothing.
    */
    std::array<OscLfo, kMaxOscillators> lfos;
    int numLfos = 0;

    /** Whether any of those is sweeping the field. Latched, so isPanned() is a
        question about this note rather than about the document. */
    bool panned = false;

    /** What setPitchModulation last worked out, so the LFO's own pitch fold can
        compose with the bend instead of re-deriving it - and so that a voice
        with no bend multiplies by exactly 1.0, which changes nothing. */
    double bendFactor = 1.0;

    /** Sets the increments of the LFO-driven oscillators for the coming block.

        Per block rather than per sample, like the vibrato it sits beside: the
        factor costs a std::pow per oscillator, and thirty updates per cycle is
        already smooth to the ear. Level and pan are per SAMPLE, in renderAdd,
        because those the ear tracks continuously and a block step in either is
        a zipper.
    */
    void applyLfoPitch() noexcept;

    /** Records the LFO for the oscillator just built, at whichever index it
        landed. Exactly one of the two indices is real; the other is -1. */
    void startLfo (const OscSettings& settings, int classicIndex, int wavetableIndex) noexcept;

    /** The FM matrix this note latched, indexed [source slot][destination
        slot], and how much of each slot is heard.

        By SLOT rather than by position in either run, because note-on skips the
        disabled slots and partitions what is left - so nothing about an index
        into `oscillators` says which row of the matrix it is.

        A slot addresses itself on the diagonal, which is feedback and needs no
        special case: every modulator is read one sample late.
    */
    std::array<std::array<float, kMaxOscillators>, kMaxOscillators> fmAmount {};
    std::array<float, kMaxOscillators> fmOut {};

    /** What each slot put out LAST sample, envelope included - the modulator
        signal every cell of the matrix reads.

        One sample late, deliberately and unavoidably: a matrix with a diagonal
        has no evaluation order that could read this sample's value, and a unit
        delay is what every FM synth with feedback has always used. At audio
        rates it is a phase error of a fraction of a degree.

        It carries the amplitude ENVELOPE and the slot's own gain, so an FM
        patch gets brighter as it is struck and duller as it decays - which is
        the only timbral movement available with one envelope per voice. It does
        NOT carry velocity or an LFO's volume swing: those shape what is heard,
        and folding them in would make a knob marked VOL change the timbre.
    */
    std::array<float, kMaxOscillators> slotOut {};

    /** Whether this note is running the matrix at all - `OscBankSnapshot::anyFm`
        as it stood at note-on. False takes the render path that predates the
        matrix, expression for expression. */
    bool fmActive = false;

    /** renderAdd's other half, in SynthVoiceFm.cpp: the same three passes with
        a phase offset threaded through them and the output column applied.

        A whole second loop rather than a branch inside the first, because the
        plain one must stay the arithmetic the pinned renders are pinned to -
        and its own translation unit because this file is already near the
        length gate. The lifecycle both share is beginSample/endSample below.
    */
    void renderAddFm (float* mono, int numSamples, float* panLeft, float* panRight) noexcept;

    /** One sample of the voice's own lifecycle, before anything is rendered.

        False when this sample contributes nothing - the note has not begun, so
        neither has its attack, and the envelope must not advance.

        Inline and shared rather than copied into the FM loop: it is where the
        release countdown, the envelope and the note's own end live, and two
        copies of that is two places a voice can learn to outlive its note.
    */
    bool beginSample (float& envelope) noexcept
    {
        if (samplesUntilStart > 0)
        {
            --samplesUntilStart;
            return false;
        }

        if (samplesUntilRelease > 0 && --samplesUntilRelease == 0)
            adsr.noteOff();

        envelope = adsr.getNextSample();
        return true;
    }

    /** The other half: ages the voice and answers whether it is still sounding.
        False means the caller breaks out of its loop. */
    bool endSample() noexcept
    {
        ++samplesSinceStart;

        if (adsr.isActive())
            return true;

        active = false;
        return false;
    }

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
