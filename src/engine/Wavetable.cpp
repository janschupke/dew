#include "engine/Wavetable.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace dew
{

namespace
{

constexpr int kNumHarmonics = kWavetableSize / 2;

int orderFor (int n) noexcept
{
    int order = 0;

    while ((1 << order) < n)
        ++order;

    return order;
}

/** Sine-series synthesis: fills `dest` with
    sum over k of amplitudes[k] * sin(2 pi k t / n).

    Done with one inverse FFT rather than by summing sines in the time domain.
    The factory set is 16 frames x 11 mips x 5 tables; summed directly that is
    a couple of hundred million std::sin calls and a visible pause at startup.

    The +/-i pair is what makes the result real and in SINE phase: bin k gets
    (0, -a) and bin n-k its conjugate, which reconstructs 2a*sin(theta). The
    convention is pinned by a test - a saw built out of cosines is not a saw.
*/
void synthesiseCycle (const std::vector<float>& amplitudes, int maxHarmonic, int n,
                      std::vector<float>& dest)
{
    const juce::dsp::FFT fft (orderFor (n));

    std::vector<juce::dsp::Complex<float>> spectrum ((size_t) n), time ((size_t) n);

    const auto limit = juce::jmin (maxHarmonic, n / 2 - 1, (int) amplitudes.size() - 1);

    for (int k = 1; k <= limit; ++k)
    {
        const auto a = amplitudes[(size_t) k];
        spectrum[(size_t) k] = { 0.0f, -a };
        spectrum[(size_t) (n - k)] = { 0.0f, a };
    }

    fft.perform (spectrum.data(), time.data(), true);

    dest.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
        dest[(size_t) i] = time[(size_t) i].real();
}

/** What to multiply a mip by so that an amplitude of 1 is a unit sine.

    Measured rather than derived, so nothing here depends on whether JUCE's
    inverse transform carries a 1/n. Every mip is scaled by its own factor,
    which is what puts them all on one absolute scale.
*/
float sineReferenceGain (int mip)
{
    static std::array<float, kWavetableMips> cache {};

    const auto m = (size_t) juce::jlimit (0, kWavetableMips - 1, mip);

    if (cache[m] <= 0.0f)
    {
        std::vector<float> amplitudes ((size_t) kNumHarmonics + 1, 0.0f);
        amplitudes[1] = 1.0f;

        std::vector<float> cycle;
        synthesiseCycle (amplitudes, 1, wavetableMipSize ((int) m), cycle);

        float peak = 0.0f;

        for (auto value : cycle)
            peak = juce::jmax (peak, std::abs (value));

        cache[m] = peak > 0.0f ? 1.0f / peak : 1.0f;
    }

    return cache[m];
}

/** The inverse of synthesiseCycle: a cycle's sine-series amplitudes.

    Only `fold` needs it - a folded sine has no closed-form spectrum - and it is
    calibrated against a known sine for the same reason the synthesis is, so the
    two round-trip whatever the transform's scaling turns out to be.
*/
void analyseCycle (const std::vector<float>& cycle, std::vector<float>& amplitudes)
{
    const auto n = (int) cycle.size();
    const juce::dsp::FFT fft (orderFor (n));

    const auto transform =
        [&fft, n] (const std::vector<float>& input, std::vector<juce::dsp::Complex<float>>& output)
    {
        std::vector<juce::dsp::Complex<float>> in ((size_t) n);

        for (int i = 0; i < n; ++i)
            in[(size_t) i] = { input[(size_t) i], 0.0f };

        output.resize ((size_t) n);
        fft.perform (in.data(), output.data(), false);
    };

    std::vector<float> reference ((size_t) n);

    for (int i = 0; i < n; ++i)
        reference[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi * (float) i
                                          / (float) n);

    std::vector<juce::dsp::Complex<float>> referenceSpectrum, spectrum;
    transform (reference, referenceSpectrum);
    transform (cycle, spectrum);

    const auto unit = referenceSpectrum[1].imag();

    if (std::abs (unit) < 1.0e-12f)
        return;

    const auto limit = juce::jmin ((int) amplitudes.size() - 1, n / 2 - 1);

    for (int k = 1; k <= limit; ++k)
        amplitudes[(size_t) k] = spectrum[(size_t) k].imag() / unit;
}

// --- the factory spectra -----------------------------------------------------
//
// Each is a formula over the frame index, not a block of data. That is what
// keeps the tables out of the repo and the renders reproducible.

float morphPosition (int frame) noexcept
{
    return (float) frame / (float) (kWavetableFrames - 1);
}

/** Harmonic k of one of the four classic shapes. The triangle's alternating
    sign is load-bearing: without it the same amplitudes make a rounded pulse.
*/
float shapeHarmonic (int shape, int k) noexcept
{
    switch (shape)
    {
        case 0: return k == 1 ? 1.0f : 0.0f;
        case 1:
            return (k % 2 == 1) ? ((((k - 1) / 2) % 2 == 0) ? 1.0f : -1.0f) / (float) (k * k)
                                : 0.0f;
        case 2: return (k % 2 == 1) ? 1.0f / (float) k : 0.0f;
        default: return 1.0f / (float) k;
    }
}

void basicSpectrum (int frame, std::vector<float>& a)
{
    const auto t = morphPosition (frame) * 3.0f;
    const auto index = juce::jlimit (0, 2, (int) t);
    const auto blend = juce::jlimit (0.0f, 1.0f, t - (float) index);

    for (int k = 1; k < (int) a.size(); ++k)
        a[(size_t) k] = (1.0f - blend) * shapeHarmonic (index, k)
                        + blend * shapeHarmonic (index + 1, k);
}

void pulseSpectrum (int frame, std::vector<float>& a)
{
    // Duty runs to 0.05 rather than to 0: a vanishing pulse is a vanishing
    // sound, and the last frame would be an expensive silence.
    const auto duty = 0.5f - 0.45f * morphPosition (frame);

    for (int k = 1; k < (int) a.size(); ++k)
        a[(size_t) k] = 2.0f / ((float) k * juce::MathConstants<float>::pi)
                        * std::sin ((float) k * juce::MathConstants<float>::pi * duty);
}

void harmonicsSpectrum (int frame, std::vector<float>& a)
{
    const auto maxHarmonic = std::pow ((float) kNumHarmonics, morphPosition (frame));

    for (int k = 1; k < (int) a.size(); ++k)
    {
        const auto over = (float) k / maxHarmonic;

        // A soft shoulder, not a brick wall. A cutoff that admits one more
        // harmonic per frame is audible as a staircase across the morph.
        a[(size_t) k] = over <= 1.0f ? 1.0f / (float) k
                                     : std::exp (-4.0f * (over - 1.0f)) / (float) k;
    }
}

void formantSpectrum (int frame, std::vector<float>& a)
{
    const auto centre = 2.0f * std::pow (48.0f, morphPosition (frame));
    const auto width = 0.35f; // octaves

    for (int k = 1; k < (int) a.size(); ++k)
    {
        const auto octaves = std::log2 ((float) k / centre) / width;

        // A saw under a resonant peak that slides up the series - which is
        // what a vowel is, near enough for an oscillator.
        a[(size_t) k] = (0.25f + 4.0f * std::exp (-0.5f * octaves * octaves)) / (float) k;
    }
}

void foldSpectrum (int frame, std::vector<float>& a)
{
    const auto drive = 1.0f + 5.0f * morphPosition (frame);

    std::vector<float> cycle ((size_t) kWavetableSize);

    for (int i = 0; i < kWavetableSize; ++i)
    {
        auto value = drive
                     * std::sin (juce::MathConstants<float>::twoPi * (float) i
                                 / (float) kWavetableSize);

        // Reflect back inside [-1, 1] rather than clipping at it. Folding is
        // odd-symmetric, so the result is still a pure sine series and the
        // sine-phase reconstruction above is exact rather than approximate.
        while (value > 1.0f || value < -1.0f)
            value = value > 1.0f ? 2.0f - value : -2.0f - value;

        cycle[(size_t) i] = value;
    }

    analyseCycle (cycle, a);
}

const std::vector<Wavetable>& bank()
{
    static const std::vector<Wavetable> tables = []
    {
        std::vector<Wavetable> built;
        built.reserve (5);
        built.emplace_back ("basic", "Basic Shapes", basicSpectrum);
        built.emplace_back ("pulse", "Pulse", pulseSpectrum);
        built.emplace_back ("harmonics", "Harmonics", harmonicsSpectrum);
        built.emplace_back ("formant", "Formant", formantSpectrum);
        built.emplace_back ("fold", "Fold", foldSpectrum);
        return built;
    }();

    return tables;
}

} // namespace

int wavetableMipFor (double phaseIncrement) noexcept
{
    if (! (phaseIncrement > 0.0))
        return 0;

    // Solves (kNumHarmonics >> m) * dt <= 0.5.
    const auto needed = std::log2 ((double) kWavetableSize * phaseIncrement);

    return juce::jlimit (0, kWavetableMips - 1, (int) std::ceil (needed));
}

Wavetable::Wavetable (juce::String tableName, juce::String tableDisplayName,
                      const FrameSpectrum& spectrum)
    : name (std::move (tableName))
    , displayName (std::move (tableDisplayName))
{
    int offset = 0;

    for (int m = 0; m < kWavetableMips; ++m)
    {
        mipOffset[(size_t) m] = offset;
        offset += wavetableMipSize (m) + 1; // the +1 is the wrapped guard sample
    }

    frameStride = offset;
    storage.assign ((size_t) frameStride * (size_t) kWavetableFrames, 0.0f);

    std::vector<float> amplitudes ((size_t) kNumHarmonics + 1, 0.0f);
    std::vector<float> cycle;

    for (int frame = 0; frame < kWavetableFrames; ++frame)
    {
        std::fill (amplitudes.begin(), amplitudes.end(), 0.0f);
        spectrum (frame, amplitudes);
        amplitudes[0] = 0.0f; // DC never travels: an offset frame clicks

        float peak = 0.0f;

        for (int m = 0; m < kWavetableMips; ++m)
        {
            const auto n = wavetableMipSize (m);
            synthesiseCycle (amplitudes, wavetableMipHarmonics (m), n, cycle);

            const auto gain = sineReferenceGain (m);
            auto* dest = storage.data() + (size_t) frame * (size_t) frameStride
                         + (size_t) mipOffset[(size_t) m];

            for (int i = 0; i < n; ++i)
                dest[i] = cycle[(size_t) i] * gain;

            if (m == 0)
                for (int i = 0; i < n; ++i)
                    peak = juce::jmax (peak, std::abs (dest[i]));
        }

        // Every mip scaled by the SAME factor, the one taken from mip 0.
        // Normalising each mip to its own peak would make a held note change
        // level as it rose through a mip boundary - a glissando in volume.
        const auto norm = peak > 1.0e-6f ? 1.0f / peak : 1.0f;

        for (int m = 0; m < kWavetableMips; ++m)
        {
            const auto n = wavetableMipSize (m);
            auto* dest = storage.data() + (size_t) frame * (size_t) frameStride
                         + (size_t) mipOffset[(size_t) m];

            for (int i = 0; i < n; ++i)
                dest[i] *= norm;

            dest[n] = dest[0];
        }
    }
}

const float* Wavetable::frameData (int frame, int mip) const noexcept
{
    const auto f = juce::jlimit (0, kWavetableFrames - 1, frame);
    const auto m = juce::jlimit (0, kWavetableMips - 1, mip);

    return storage.data() + (size_t) f * (size_t) frameStride + (size_t) mipOffset[(size_t) m];
}

float Wavetable::sampleAt (int frame, int mip, double phase) const noexcept
{
    const auto n = wavetableMipSize (mip);
    const auto x = phase * (double) n;

    auto index = (int) x;

    if (index < 0)
        index = 0;
    else if (index >= n)
        index = n - 1;

    const auto fraction = (float) (x - (double) index);
    const auto* samples = frameData (frame, mip);

    // Reads samples[n], which is why every mip stores a wrapped guard sample.
    return samples[index] + fraction * (samples[index + 1] - samples[index]);
}

float Wavetable::at (float position, int mip, double phase) const noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, position) * (float) (kWavetableFrames - 1);
    const auto lower = juce::jlimit (0, kWavetableFrames - 1, (int) x);
    const auto upper = juce::jlimit (0, kWavetableFrames - 1, lower + 1);
    const auto blend = juce::jlimit (0.0f, 1.0f, x - (float) lower);

    const auto a = sampleAt (lower, mip, phase);
    const auto b = sampleAt (upper, mip, phase);

    return a + blend * (b - a);
}

int wavetableCount() noexcept
{
    return (int) bank().size();
}

const Wavetable& wavetableAt (int index) noexcept
{
    const auto& tables = bank();

    return tables[(size_t) juce::jlimit (0, (int) tables.size() - 1, index)];
}

int wavetableIndexFor (const juce::String& name) noexcept
{
    const auto& tables = bank();

    for (size_t i = 0; i < tables.size(); ++i)
        if (tables[i].getName() == name)
            return (int) i;

    return -1;
}

} // namespace dew
