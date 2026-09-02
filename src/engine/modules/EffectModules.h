#pragma once

#include <juce_dsp/juce_dsp.h>

#include "engine/Biquad.h"
#include "engine/Module.h"
#include "model/Constants.h"

namespace dew
{

/** The six effects, each behind the module interface.

    Every process() body below was moved out of EffectUnit unchanged, with one
    substitution: `params.cutoff` became `p[kCutoff]`. Nothing was reformatted,
    nothing hoisted, no multiply re-associated. Float arithmetic is not
    associative, and four separate test files pin dew's renders sample for
    sample, so the only safe way to move DSP is to move it literally.

    Each module names its own parameter indices. They are the descriptor's order
    after the common block - which the catalog owns, and which CatalogTests
    checks against these names, so a table reordered without touching this file
    fails rather than silently driving the wrong knob.
*/

class FilterModule final : public EffectModule
{
public:
    enum { kMode = 0, kCutoff = 1, kResonance = 2 };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    juce::dsp::StateVariableTPTFilter<float> filter;
};

class ReverbModule final : public EffectModule
{
public:
    enum { kRoomSize = 0, kDamping = 1, kWidth = 2 };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::Reverb reverb;
};

class DelayModule final : public EffectModule
{
public:
    enum { kDelayMs = 0, kFeedback = 1 };

    /** Longest delay the line can be set to. Fixed so it is allocated once. */
    static constexpr float maxDelayMs = 1000.0f;

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 2 };
    float feedbackState[2] { 0.0f, 0.0f };
};

class DriveModule final : public EffectModule
{
public:
    enum { kDrive = 0, kOutputGain = 1 };

    void prepare (double, int) override {}
    void reset() noexcept override {}
    void process (ParamBlock, StereoView) noexcept override;
};

class ChorusModule final : public EffectModule
{
public:
    enum { kRate = 0, kDepth = 1 };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::dsp::Chorus<float> chorus;
};

class EqModule final : public EffectModule
{
public:
    enum { kLowGainDb = 0, kMidGainDb = 1, kMidFreq = 2, kHighGainDb = 3 };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    Biquad low[2], mid[2], high[2];
};

} // namespace dew
