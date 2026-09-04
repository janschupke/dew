#pragma once

#include "engine/Biquad.h"
#include "engine/SoundFont.h"

namespace dew
{

/** How a channel's knobs bend the font it points at, resolved for one block.

    Offsets rather than values, the way an SF2 preset colours an instrument it
    does not own: the font stays authoritative and these move it. Trivially
    copyable, because it travels in the snapshot.
*/
struct SoundFontSettings
{
    int bank = 0;
    int program = 0;

    float transposeSemitones = 0.0f;
    float tuneCents = 0.0f;
    float filterOffsetCents = 0.0f;
    float attackScale = 1.0f;
    float releaseScale = 1.0f;
    float velocitySensitivity = 1.0f;
};

/** One region of one soundfont, sounding.

    A voice is a REGION rather than a note, which is the difference between this
    and SynthVoice: one note-on starts several - a velocity layer and both
    halves of a stereo pair are separate regions of the same preset - so several
    voices answer to one note-off and the pitch is what identifies them.

    The font is held by pointer and never copied. Safe for the reason
    SynthVoice's `const Wavetable*` is: it lives in the snapshot as a
    shared_ptr, and a snapshot the audio thread is reading is never released on
    the audio thread.
*/
class SoundFontVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void start (const SoundFontData& font, const SoundFontRegion& region, int notePitch,
                float velocity, const SoundFontSettings& settings, int durationSamples,
                int startOffset) noexcept;

    /** Moves to the release stage without cutting the tail. */
    void release() noexcept;

    /** Silences the voice at once. What an exclusive class does to the voice it
        replaces - a closed hi-hat cuts an open one rather than letting it ring
        down, which is the whole point of the mechanism. */
    void cut() noexcept;

    /** ADDS this voice into a stereo pair. */
    void renderAdd (float* left, float* right, int numSamples) noexcept;

    bool isActive() const noexcept
    {
        return active;
    }
    int getPitch() const noexcept
    {
        return pitch;
    }
    int getExclusiveClass() const noexcept
    {
        return exclusiveClass;
    }

    /** Samples rendered since this voice started, so the oldest can be stolen. */
    juce::uint32 getAge() const noexcept
    {
        return age;
    }

private:
    /** The format's six stages, run in its order.

        `hold` is a stage of its own rather than part of the attack: a
        percussive region holds its peak for a fixed time before decaying, and
        folding the two together would make the peak arrive late instead of
        lasting.

        Decay and release fall at a fixed rate in DECIBELS, which is what the
        format means by their times - a hundred decibels over the stated time -
        so each is one multiply per sample rather than a pow.
    */
    struct Envelope
    {
        enum class Stage
        {
            idle,
            delay,
            attack,
            hold,
            decay,
            sustain,
            release
        };

        void start (const SoundFontEnvelope&, double sampleRate, float attackScale,
                    float releaseScale) noexcept;
        void release() noexcept;
        float nextSample() noexcept;

        bool isFinished() const noexcept
        {
            return stage == Stage::idle;
        }

        Stage stage = Stage::idle;
        float level = 0.0f;
        float sustainLevel = 1.0f;
        int samplesLeft = 0;
        float attackIncrement = 0.0f;
        float decayFactor = 1.0f;
        float releaseFactor = 1.0f;
        int holdSamples = 0;
        int attackSamples = 0;
    };

    void updateFilter() noexcept;

    const SoundFontData* font = nullptr;
    const SoundFontRegion* region = nullptr;

    double phase = 0.0;     ///< read position in source samples, from the region's start
    double increment = 1.0; ///< source samples per output sample

    int pitch = 0;
    int exclusiveClass = 0;
    float gain = 0.0f; ///< velocity, the region's attenuation and sensitivity, folded
    float leftGain = 1.0f, rightGain = 1.0f;

    Envelope amplitude, modulation;

    int samplesUntilStart = 0;
    int samplesUntilRelease = -1; ///< -1 means held, not timed

    bool active = false;
    bool filtered = false;
    float filterBaseCents = 13500.0f;
    float modToFilterCents = 0.0f;
    float filterQDb = 0.0f;

    Biquad filterLeft, filterRight;

    double engineSampleRate = 44100.0;
    juce::uint32 age = 0;
};

} // namespace dew
