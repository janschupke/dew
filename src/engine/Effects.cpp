#include "Effects.h"

namespace dew
{

// --- names -------------------------------------------------------------------

EffectType effectTypeFromString (const juce::String& s)
{
    if (s == "reverb") return EffectType::reverb;
    if (s == "delay")  return EffectType::delay;
    if (s == "drive")  return EffectType::drive;
    if (s == "chorus") return EffectType::chorus;
    if (s == "eq")     return EffectType::eq;

    return EffectType::filter;
}

juce::String effectTypeToString (EffectType type)
{
    switch (type)
    {
        case EffectType::reverb: return "reverb";
        case EffectType::delay:  return "delay";
        case EffectType::drive:  return "drive";
        case EffectType::chorus: return "chorus";
        case EffectType::eq:     return "eq";
        case EffectType::filter: break;
    }

    return "filter";
}

juce::String effectTypeDisplayName (EffectType type)
{
    switch (type)
    {
        case EffectType::reverb: return "Reverb";
        case EffectType::delay:  return "Delay";
        case EffectType::drive:  return "Drive";
        case EffectType::chorus: return "Chorus";
        case EffectType::eq:     return "EQ";
        case EffectType::filter: break;
    }

    return "Filter";
}

FilterMode filterModeFromString (const juce::String& s)
{
    if (s == "highpass") return FilterMode::highpass;
    if (s == "bandpass") return FilterMode::bandpass;

    return FilterMode::lowpass;
}

juce::String filterModeToString (FilterMode mode)
{
    switch (mode)
    {
        case FilterMode::highpass: return "highpass";
        case FilterMode::bandpass: return "bandpass";
        case FilterMode::lowpass:  break;
    }

    return "lowpass";
}

// --- Biquad ------------------------------------------------------------------

void Biquad::setCoefficients (double b0n, double b1n, double b2n,
                              double a0n, double a1n, double a2n) noexcept
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
    const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
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
    const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
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
    const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.49, (double) frequency) / sampleRate;
    const auto cosw = std::cos (w), sinw = std::sin (w);
    const auto alpha = sinw / (2.0 * juce::jmax (0.05, (double) q));

    setCoefficients (1.0 + alpha * A,
                     -2.0 * cosw,
                     1.0 - alpha * A,
                     1.0 + alpha / A,
                     -2.0 * cosw,
                     1.0 - alpha / A);
}

// --- EffectUnit --------------------------------------------------------------

EffectUnit::EffectUnit() = default;

void EffectUnit::prepare (double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    blockSize = juce::jmax (1, maximumBlockSize);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 2 };

    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    reverb.setSampleRate (sampleRate);

    // Sized for the longest delay the parameter range allows, so changing the
    // delay time never reallocates.
    delayLine.setMaximumDelayInSamples ((int) (maxDelayMs * 0.001 * sampleRate) + 4);
    delayLine.prepare (spec);

    chorus.prepare (spec);

    dry.setSize (2, blockSize);

    reset();
}

void EffectUnit::reset() noexcept
{
    filter.reset();
    reverb.reset();
    delayLine.reset();
    chorus.reset();

    for (int i = 0; i < 2; ++i)
    {
        eqLow[i].reset();
        eqMid[i].reset();
        eqHigh[i].reset();
        delayFeedbackState[i] = 0.0f;
    }
}

void EffectUnit::process (EffectType type, const EffectParams& params,
                          float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr || right == nullptr)
        return;

    const auto mix = juce::jlimit (0.0f, 1.0f, params.mix);

    // A fully dry effect is not worth the cycles, and skipping it also means a
    // bypassed slot cannot colour the signal at all.
    if (mix <= 0.0f)
        return;

    const auto keepDry = mix < 1.0f && numSamples <= dry.getNumSamples();

    if (keepDry)
    {
        juce::FloatVectorOperations::copy (dry.getWritePointer (0), left, numSamples);
        juce::FloatVectorOperations::copy (dry.getWritePointer (1), right, numSamples);
    }

    switch (type)
    {
        case EffectType::filter: processFilter (params, left, right, numSamples); break;
        case EffectType::reverb: processReverb (params, left, right, numSamples); break;
        case EffectType::delay:  processDelay  (params, left, right, numSamples); break;
        case EffectType::drive:  processDrive  (params, left, right, numSamples); break;
        case EffectType::chorus: processChorus (params, left, right, numSamples); break;
        case EffectType::eq:     processEq     (params, left, right, numSamples); break;
    }

    if (keepDry)
    {
        juce::FloatVectorOperations::multiply (left, mix, numSamples);
        juce::FloatVectorOperations::multiply (right, mix, numSamples);
        juce::FloatVectorOperations::addWithMultiply (left, dry.getReadPointer (0), 1.0f - mix, numSamples);
        juce::FloatVectorOperations::addWithMultiply (right, dry.getReadPointer (1), 1.0f - mix, numSamples);
    }
}

void EffectUnit::processFilter (const EffectParams& params, float* left, float* right,
                                int numSamples) noexcept
{
    switch (params.filterMode)
    {
        case FilterMode::highpass: filter.setType (juce::dsp::StateVariableTPTFilterType::highpass); break;
        case FilterMode::bandpass: filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FilterMode::lowpass:  filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass); break;
    }

    filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRate * 0.45), params.cutoff));
    filter.setResonance (juce::jlimit (0.05f, 4.0f, params.resonance));

    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = filter.processSample (0, left[i]);
        right[i] = filter.processSample (1, right[i]);
    }
}

void EffectUnit::processReverb (const EffectParams& params, float* left, float* right,
                                int numSamples) noexcept
{
    juce::Reverb::Parameters p;
    p.roomSize = juce::jlimit (0.0f, 1.0f, params.roomSize);
    p.damping = juce::jlimit (0.0f, 1.0f, params.damping);
    p.width = juce::jlimit (0.0f, 1.0f, params.width);

    // The unit's own wet/dry runs the mix, so the reverb itself is fully wet -
    // otherwise `mix` would be applied twice and never reach a full tail.
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.freezeMode = 0.0f;

    reverb.setParameters (p);
    reverb.processStereo (left, right, numSamples);
}

void EffectUnit::processDelay (const EffectParams& params, float* left, float* right,
                               int numSamples) noexcept
{
    const auto delaySamples = juce::jlimit (1.0f, maxDelayMs * 0.001f * (float) sampleRate,
                                            params.delayMs * 0.001f * (float) sampleRate);
    delayLine.setDelay (delaySamples);

    const auto feedback = juce::jlimit (0.0f, 0.95f, params.feedback);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i], inR = right[i];

        delayLine.pushSample (0, inL + delayFeedbackState[0] * feedback);
        delayLine.pushSample (1, inR + delayFeedbackState[1] * feedback);

        const auto outL = delayLine.popSample (0);
        const auto outR = delayLine.popSample (1);

        delayFeedbackState[0] = outL;
        delayFeedbackState[1] = outR;

        left[i] = outL;
        right[i] = outR;
    }
}

void EffectUnit::processDrive (const EffectParams& params, float* left, float* right,
                               int numSamples) noexcept
{
    const auto drive = juce::jlimit (1.0f, 40.0f, params.drive);
    const auto gain = juce::jlimit (0.0f, 4.0f, params.outputGain);

    // tanh saturation, normalised so raising the drive changes the character
    // rather than only the level.
    const auto normalise = 1.0f / std::tanh (drive);

    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = std::tanh (left[i] * drive) * normalise * gain;
        right[i] = std::tanh (right[i] * drive) * normalise * gain;
    }
}

void EffectUnit::processChorus (const EffectParams& params, float* left, float* right,
                                int numSamples) noexcept
{
    chorus.setRate (juce::jlimit (0.01f, 20.0f, params.rate));
    chorus.setDepth (juce::jlimit (0.0f, 1.0f, params.depth));
    chorus.setCentreDelay (12.0f);
    chorus.setFeedback (0.0f);
    chorus.setMix (1.0f);

    float* data[2] { left, right };
    juce::dsp::AudioBlock<float> block (data, 2, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    chorus.process (context);
}

void EffectUnit::processEq (const EffectParams& params, float* left, float* right,
                            int numSamples) noexcept
{
    const auto midFreq = juce::jlimit (100.0f, 8000.0f, params.midFreq);

    for (int c = 0; c < 2; ++c)
    {
        eqLow[c].setLowShelf (sampleRate, 200.0f, juce::jlimit (-24.0f, 24.0f, params.lowGainDb));
        eqMid[c].setPeak (sampleRate, midFreq, 0.9f, juce::jlimit (-24.0f, 24.0f, params.midGainDb));
        eqHigh[c].setHighShelf (sampleRate, 4000.0f, juce::jlimit (-24.0f, 24.0f, params.highGainDb));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = eqHigh[0].processSample (eqMid[0].processSample (eqLow[0].processSample (left[i])));
        right[i] = eqHigh[1].processSample (eqMid[1].processSample (eqLow[1].processSample (right[i])));
    }
}

} // namespace dew
