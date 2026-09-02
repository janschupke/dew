#include "engine/modules/EffectModules.h"

#include <cmath>

#include "model/EffectType.h"

namespace dew
{

// --- filter ------------------------------------------------------------------

void FilterModule::prepare (double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;

    const juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };

    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.reset();
}

void FilterModule::reset() noexcept
{
    filter.reset();
}

void FilterModule::process (ParamBlock p, StereoView io) noexcept
{
    switch ((FilterMode) p.choice (kMode, 3))
    {
        case FilterMode::highpass: filter.setType (juce::dsp::StateVariableTPTFilterType::highpass); break;
        case FilterMode::bandpass: filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FilterMode::lowpass:  filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass); break;
    }

    // Not a range: what the device can represent. Cutoff is declared up to
    // 20kHz and at 44.1k this filter cannot go above about 19.8k, so the clamp
    // is load-bearing rather than defensive.
    filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (rate * 0.45), p[kCutoff]));
    filter.setResonance (p[kResonance]);

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i]  = filter.processSample (0, io.left[i]);
        io.right[i] = filter.processSample (1, io.right[i]);
    }
}

// --- reverb ------------------------------------------------------------------

void ReverbModule::prepare (double sampleRate, int)
{
    reverb.setSampleRate (sampleRate > 0.0 ? sampleRate : kDefaultSampleRate);
    reverb.reset();
}

void ReverbModule::reset() noexcept
{
    reverb.reset();
}

void ReverbModule::process (ParamBlock p, StereoView io) noexcept
{
    juce::Reverb::Parameters params;
    params.roomSize = p[kRoomSize];
    params.damping = p[kDamping];
    params.width = p[kWidth];

    // The host runs the wet/dry mix, so the reverb itself is fully wet -
    // otherwise `mix` would be applied twice and never reach a full tail.
    params.wetLevel = 1.0f;
    params.dryLevel = 0.0f;
    params.freezeMode = 0.0f;

    reverb.setParameters (params);
    reverb.processStereo (io.left, io.right, io.numSamples);
}

// --- delay -------------------------------------------------------------------

void DelayModule::prepare (double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;

    const juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };

    // Sized for the longest delay the parameter range allows, so changing the
    // delay time never reallocates.
    line.setMaximumDelayInSamples ((int) (maxDelayMs * 0.001 * rate) + 4);
    line.prepare (spec);
    reset();
}

void DelayModule::reset() noexcept
{
    line.reset();
    feedbackState[0] = 0.0f;
    feedbackState[1] = 0.0f;
}

void DelayModule::process (ParamBlock p, StereoView io) noexcept
{
    const auto delaySamples = juce::jlimit (1.0f, maxDelayMs * 0.001f * (float) rate,
                                            p[kDelayMs] * 0.001f * (float) rate);
    line.setDelay (delaySamples);

    const auto feedback = p[kFeedback];

    for (int i = 0; i < io.numSamples; ++i)
    {
        const auto inL = io.left[i], inR = io.right[i];

        line.pushSample (0, inL + feedbackState[0] * feedback);
        line.pushSample (1, inR + feedbackState[1] * feedback);

        const auto outL = line.popSample (0);
        const auto outR = line.popSample (1);

        feedbackState[0] = outL;
        feedbackState[1] = outR;

        io.left[i] = outL;
        io.right[i] = outR;
    }
}

// --- drive -------------------------------------------------------------------

void DriveModule::process (ParamBlock p, StereoView io) noexcept
{
    const auto drive = p[kDrive];
    const auto gain = p[kOutputGain];

    // tanh saturation, normalised so raising the drive changes the character
    // rather than only the level.
    const auto normalise = 1.0f / std::tanh (drive);

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i]  = std::tanh (io.left[i] * drive) * normalise * gain;
        io.right[i] = std::tanh (io.right[i] * drive) * normalise * gain;
    }
}

// --- chorus ------------------------------------------------------------------

void ChorusModule::prepare (double sampleRate, int maximumBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate > 0.0 ? sampleRate : kDefaultSampleRate,
                                        (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };
    chorus.prepare (spec);
    chorus.reset();
}

void ChorusModule::reset() noexcept
{
    chorus.reset();
}

void ChorusModule::process (ParamBlock p, StereoView io) noexcept
{
    chorus.setRate (p[kRate]);
    chorus.setDepth (p[kDepth]);
    chorus.setCentreDelay (12.0f);
    chorus.setFeedback (0.0f);
    chorus.setMix (1.0f);

    float* data[2] { io.left, io.right };
    juce::dsp::AudioBlock<float> block (data, 2, (size_t) io.numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    chorus.process (context);
}

// --- eq ----------------------------------------------------------------------

void EqModule::prepare (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
    reset();
}

void EqModule::reset() noexcept
{
    for (int i = 0; i < 2; ++i)
    {
        low[i].reset();
        mid[i].reset();
        high[i].reset();
    }
}

void EqModule::process (ParamBlock p, StereoView io) noexcept
{
    const auto midFreq = p[kMidFreq];

    for (int c = 0; c < 2; ++c)
    {
        low[c].setLowShelf (rate, 200.0f, p[kLowGainDb]);
        mid[c].setPeak (rate, midFreq, 0.9f, p[kMidGainDb]);
        high[c].setHighShelf (rate, 4000.0f, p[kHighGainDb]);
    }

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i]  = high[0].processSample (mid[0].processSample (low[0].processSample (io.left[i])));
        io.right[i] = high[1].processSample (mid[1].processSample (low[1].processSample (io.right[i])));
    }
}

} // namespace dew
