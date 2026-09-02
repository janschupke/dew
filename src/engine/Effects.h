#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "model/Constants.h"

namespace dew
{

/** The effects a chain slot can hold. Ordered as they appear in the picker. */
enum class EffectType { filter, reverb, delay, drive, chorus, eq };

EffectType effectTypeFromString (const juce::String&);
juce::String effectTypeToString (EffectType);
juce::String effectTypeDisplayName (EffectType);

enum class FilterMode { lowpass, highpass, bandpass };

FilterMode filterModeFromString (const juce::String&);
juce::String filterModeToString (FilterMode);

/** Every parameter of every effect, flat.

    One struct rather than a variant per type: the audio thread reads whichever
    fields its type uses and ignores the rest, and the file format stays a single
    declared node with real defaults and validation for each property. It costs
    a few dozen bytes per slot, which is nothing next to the DSP state.
*/
struct EffectParams
{
    float mix = 1.0f;               ///< dry/wet, every type

    // filter
    FilterMode filterMode = FilterMode::lowpass;
    float cutoff = 1200.0f;
    float resonance = 0.4f;

    // reverb
    float roomSize = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;

    // delay
    float delayMs = 250.0f;
    float feedback = 0.35f;

    // drive
    float drive = 2.0f;
    float outputGain = 1.0f;

    // chorus
    float rate = 1.2f;
    float depth = 0.3f;

    // eq
    float lowGainDb = 0.0f;
    float midGainDb = 0.0f;
    float midFreq = 900.0f;
    float highGainDb = 0.0f;
};

/** A direct-form-II transposed biquad whose coefficients are computed in place.

    juce::dsp::IIR::Filter would be the obvious choice, but assigning new
    coefficients allocates a ReferenceCountedObject, and these are recomputed on
    the audio thread whenever a parameter moves.
*/
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    void setLowShelf (double sampleRate, float frequency, float gainDb) noexcept;
    void setPeak (double sampleRate, float frequency, float q, float gainDb) noexcept;
    void setHighShelf (double sampleRate, float frequency, float gainDb) noexcept;

    float processSample (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void setCoefficients (double b0n, double b1n, double b2n, double a0n, double a1n, double a2n) noexcept;

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

/** One slot's worth of DSP state, holding every effect type at once.

    The audio thread must not allocate, so nothing here is built on demand:
    prepare() sizes all six at their maximum, and switching a slot from delay to
    reverb is a reset(), not a construction. That is what lets effect topology
    travel in the snapshot like every other piece of project state, instead of
    needing a command queue and a pointer swap.

    The cost is memory: a unit is a few hundred kilobytes, mostly the delay line
    and the reverb tanks, which is why the pool is capped rather than one unit
    per possible slot.
*/
class EffectUnit
{
public:
    EffectUnit();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    /** Processes in place. Wet/dry is applied here, so `mix` behaves the same
        way for every type.
    */
    void process (EffectType type, const EffectParams& params,
                  float* left, float* right, int numSamples) noexcept;

    /** Longest delay a delay effect can be set to. Fixed so the line can be
        allocated once.
    */
    static constexpr float maxDelayMs = 1000.0f;

private:
    void processFilter (const EffectParams&, float*, float*, int) noexcept;
    void processReverb (const EffectParams&, float*, float*, int) noexcept;
    void processDelay (const EffectParams&, float*, float*, int) noexcept;
    void processDrive (const EffectParams&, float*, float*, int) noexcept;
    void processChorus (const EffectParams&, float*, float*, int) noexcept;
    void processEq (const EffectParams&, float*, float*, int) noexcept;

    double sampleRate = kDefaultSampleRate;
    int blockSize = kDefaultBlockSize;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 2 };
    juce::dsp::Chorus<float> chorus;
    Biquad eqLow[2], eqMid[2], eqHigh[2];

    // Dry copy for the wet/dry mix, sized in prepare().
    juce::AudioBuffer<float> dry;

    // Feedback state for the delay, carried across blocks.
    float delayFeedbackState[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE (EffectUnit)
};

} // namespace dew
