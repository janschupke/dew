#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace dew
{

/** A direct-form-II transposed biquad whose coefficients are computed in place.

    juce::dsp::IIR::Filter would be the obvious choice, but assigning new
    coefficients allocates a ReferenceCountedObject, and these are recomputed on
    the audio thread whenever a parameter moves.
*/
class Biquad
{
public:
    void reset() noexcept
    {
        z1 = z2 = 0.0f;
    }

    /** A resonant low-pass, which is the shape a sampler's per-region filter
        is: SoundFont declares a cutoff in absolute cents and a Q in decibels,
        and has no other filter type. The three shelving shapes above are the
        EQ effect's; this one is here beside them rather than in the sampler
        because a biquad is a biquad. */
    void setLowPass (double sampleRate, float frequency, float qDb) noexcept;

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
    void setCoefficients (double b0n, double b1n, double b2n, double a0n, double a1n,
                          double a2n) noexcept;

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace dew
