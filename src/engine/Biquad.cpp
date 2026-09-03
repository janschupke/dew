#include "engine/Biquad.h"

#include <cmath>

namespace dew
{

void Biquad::setCoefficients (double b0n, double b1n, double b2n, double a0n, double a1n,
                              double a2n) noexcept
{
    // a0 of zero would be a division by zero rather than a filter; it cannot
    // happen for the shapes below, but the guard costs nothing on the audio
    // thread and a NaN propagates through the whole mix.
    const auto inverse = std::abs (a0n) > 1.0e-12 ? 1.0 / a0n : 1.0;

    b0 = (float) (b0n * inverse);
    b1 = (float) (b1n * inverse);
    b2 = (float) (b2n * inverse);
    a1 = (float) (a1n * inverse);
    a2 = (float) (a2n * inverse);
}

void Biquad::setLowShelf (double sampleRate, float frequency, float gainDb) noexcept
{
    const auto A = std::pow (10.0, (double) gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi
                   * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
    const auto cosw = std::cos (w), sinw = std::sin (w);
    const auto alpha = sinw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

    setCoefficients (A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha),
                     2.0 * A * ((A - 1.0) - (A + 1.0) * cosw),
                     A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha),
                     (A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha,
                     -2.0 * ((A - 1.0) + (A + 1.0) * cosw),
                     (A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha);
}

void Biquad::setHighShelf (double sampleRate, float frequency, float gainDb) noexcept
{
    const auto A = std::pow (10.0, (double) gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi
                   * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
    const auto cosw = std::cos (w), sinw = std::sin (w);
    const auto alpha = sinw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
    const auto twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

    setCoefficients (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha),
                     -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw),
                     A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha),
                     (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha,
                     2.0 * ((A - 1.0) - (A + 1.0) * cosw),
                     (A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha);
}

void Biquad::setPeak (double sampleRate, float frequency, float q, float gainDb) noexcept
{
    const auto A = std::pow (10.0, (double) gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi
                   * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
    const auto cosw = std::cos (w), sinw = std::sin (w);
    const auto alpha = sinw / (2.0 * juce::jmax (0.05, (double) q));

    setCoefficients (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cosw,
                     1.0 - alpha / A);
}

} // namespace dew
