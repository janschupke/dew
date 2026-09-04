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
        case FilterMode::highpass:
            filter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            break;
        case FilterMode::bandpass:
            filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            break;
        case FilterMode::lowpass:
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            break;
    }

    // Not a range: what the device can represent. Cutoff is declared up to
    // 20kHz and at 44.1k this filter cannot go above about 19.8k, so the clamp
    // is load-bearing rather than defensive.
    filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (rate * 0.45), p[kCutoff]));
    filter.setResonance (p[kResonance]);

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i] = filter.processSample (0, io.left[i]);
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
        io.left[i] = std::tanh (io.left[i] * drive) * normalise * gain;
        io.right[i] = std::tanh (io.right[i] * drive) * normalise * gain;
    }
}

// --- distortion --------------------------------------------------------------

void DistortionModule::prepare (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
    reset();
}

void DistortionModule::reset() noexcept
{
    toneState[0] = 0.0f;
    toneState[1] = 0.0f;
}

namespace
{

/** The four shapers, each on a signal already multiplied by `drive`.

    Normalisation is per shaper rather than shared, because the four reach full
    scale at different inputs and one divisor would make three of them quieter
    than they should be as the drive comes up.
*/
float shape (DistortionMode mode, float x, float drive) noexcept
{
    switch (mode)
    {
        case DistortionMode::softClip:
        {
            // The cubic soft clip, not tanh: Drive is already the tanh one, and
            // the point of a second saturator is that it sounds different.
            const auto y = juce::jlimit (-1.0f, 1.0f, x * drive);
            return 1.5f * (y - y * y * y / 3.0f);
        }

        case DistortionMode::hardClip: return juce::jlimit (-1.0f, 1.0f, x * drive);

        case DistortionMode::fold:
        {
            // A triangle fold: linear through the middle, and every time the
            // driven signal passes 1 it turns around instead of flattening.
            const auto y = x * drive;
            const auto t = 0.25f * y + 0.25f;
            return 4.0f * std::abs (t - std::floor (t + 0.5f)) - 1.0f;
        }

        case DistortionMode::crush:
        {
            // Level quantisation. Drive picks how many levels are left - in
            // BITS rather than in levels, because a bitcrusher's audible range
            // is about six bits wide and a linear map across 512 levels spends
            // three quarters of the control on steps too small to hear.
            const auto levels = std::floor (
                std::pow (2.0f, juce::jmap (drive, 1.0f, 40.0f, 6.0f, 1.0f)));
            return std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels;
        }
    }

    return x;
}

} // namespace

void DistortionModule::process (ParamBlock p, StereoView io) noexcept
{
    const auto mode = (DistortionMode) p.choice (kMode, 4);
    const auto drive = juce::jmax (1.0f, p[kDrive]);
    const auto gain = p[kOutputGain];

    // The tone control is AFTER the shaper, which is the only place it can take
    // back the harmonics the shaper just made. A one-pole is enough: this is a
    // tilt, not a filter slot.
    const auto cutoff = juce::jlimit (20.0f, (float) (rate * 0.45),
                                      300.0f
                                          * std::pow (60.0f, juce::jlimit (0.0f, 1.0f, p[kTone])));
    const auto coefficient = 1.0f
                             - std::exp (-juce::MathConstants<float>::twoPi * cutoff
                                         / (float) rate);

    float* channels[2] { io.left, io.right };

    for (int c = 0; c < 2; ++c)
    {
        auto* data = channels[c];
        auto state = toneState[c];

        for (int i = 0; i < io.numSamples; ++i)
        {
            state += coefficient * (shape (mode, data[i], drive) - state);
            data[i] = state * gain;
        }

        toneState[c] = state;
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

// --- phaser ------------------------------------------------------------------

void PhaserModule::prepare (double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;

    const juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };
    phaser.prepare (spec);
    phaser.reset();
}

void PhaserModule::reset() noexcept
{
    phaser.reset();
}

void PhaserModule::process (ParamBlock p, StereoView io) noexcept
{
    // Every clamp here is against a jassert in juce::dsp::Phaser, not against a
    // taste. An automation curve can put a parameter anywhere in its declared
    // range and the declared range is wider than what the widget accepts at
    // this sample rate, so the clamp is where the two are reconciled.
    phaser.setRate (juce::jlimit (0.01f, 99.0f, p[kRate]));
    phaser.setDepth (juce::jlimit (0.0f, 1.0f, p[kDepth]));
    phaser.setCentreFrequency (juce::jlimit (20.0f, (float) (rate * 0.45), p[kCentreFreq]));
    phaser.setFeedback (juce::jlimit (-0.95f, 0.95f, p[kFeedback]));

    // The host runs dry/wet, so the widget is fully wet - the same reason the
    // reverb and the chorus are.
    phaser.setMix (1.0f);

    float* data[2] { io.left, io.right };
    juce::dsp::AudioBlock<float> block (data, 2, (size_t) io.numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    phaser.process (context);
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
        io.left[i] = high[0].processSample (
            mid[0].processSample (low[0].processSample (io.left[i])));
        io.right[i] = high[1].processSample (
            mid[1].processSample (low[1].processSample (io.right[i])));
    }
}

// --- compressor --------------------------------------------------------------

void CompressorModule::prepare (double sampleRate, int maximumBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate > 0.0 ? sampleRate : kDefaultSampleRate,
                                        (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };
    compressor.prepare (spec);
    compressor.reset();
}

void CompressorModule::reset() noexcept
{
    compressor.reset();
}

void CompressorModule::process (ParamBlock p, StereoView io) noexcept
{
    compressor.setThreshold (p[kThreshold]);

    // juce::dsp::Compressor asserts on a ratio below 1, and a curve drawn to the
    // bottom of the declared range lands exactly there.
    compressor.setRatio (juce::jmax (1.0f, p[kRatio]));
    compressor.setAttack (juce::jmax (0.0f, p[kAttackMs]));
    compressor.setRelease (juce::jmax (0.0f, p[kReleaseMs]));

    // Applied here rather than asked of the widget, which has no makeup gain:
    // it is a hard-knee VCA and nothing else.
    const auto makeup = juce::Decibels::decibelsToGain (p[kMakeup]);

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i] = compressor.processSample (0, io.left[i]) * makeup;
        io.right[i] = compressor.processSample (1, io.right[i]) * makeup;
    }
}

// --- limiter -----------------------------------------------------------------

void LimiterModule::prepare (double sampleRate, int maximumBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate > 0.0 ? sampleRate : kDefaultSampleRate,
                                        (juce::uint32) juce::jmax (1, maximumBlockSize), 2 };
    limiter.prepare (spec);
    limiter.reset();
}

void LimiterModule::reset() noexcept
{
    limiter.reset();
}

void LimiterModule::process (ParamBlock p, StereoView io) noexcept
{
    const auto ceiling = p[kCeiling];

    limiter.setThreshold (ceiling);
    limiter.setRatio (ratio);
    limiter.setAttack (attackMs);
    limiter.setRelease (juce::jmax (0.0f, p[kReleaseMs]));

    // The clip is not belt and braces: no finite attack catches the first
    // sample of a transient, and "nothing gets past the ceiling" is the one
    // thing a limiter is for.
    const auto peak = juce::Decibels::decibelsToGain (ceiling);

    for (int i = 0; i < io.numSamples; ++i)
    {
        io.left[i] = juce::jlimit (-peak, peak, limiter.processSample (0, io.left[i]));
        io.right[i] = juce::jlimit (-peak, peak, limiter.processSample (1, io.right[i]));
    }
}

} // namespace dew
