#pragma once

#include <juce_dsp/juce_dsp.h>

#include "engine/Biquad.h"
#include "engine/Module.h"
#include "model/Constants.h"

namespace dew
{

/** The ten effects, each behind the module interface.

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
    enum
    {
        kMode = 0,
        kCutoff = 1,
        kResonance = 2
    };

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
    enum
    {
        kRoomSize = 0,
        kDamping = 1,
        kWidth = 2
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::Reverb reverb;
};

class DelayModule final : public EffectModule
{
public:
    enum
    {
        kDelayMs = 0,
        kFeedback = 1
    };

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
    enum
    {
        kDrive = 0,
        kOutputGain = 1
    };

    void prepare (double, int) override {}
    void reset() noexcept override {}
    void process (ParamBlock, StereoView) noexcept override;
};

/** The multi-mode shaper, beside Drive rather than instead of it.

    Drive is one curve - tanh - and stays that, so nothing saved has to migrate.
    This is the aggressive one: four shapers, and a tone control that is a
    one-pole lowpass after the shaper, which is where the harmonics it makes
    can actually be taken back off.
*/
class DistortionModule final : public EffectModule
{
public:
    enum
    {
        kMode = 0,
        kDrive = 1,
        kTone = 2,
        kOutputGain = 3
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    float toneState[2] { 0.0f, 0.0f };
};

class ChorusModule final : public EffectModule
{
public:
    enum
    {
        kRate = 0,
        kDepth = 1
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::dsp::Chorus<float> chorus;
};

class PhaserModule final : public EffectModule
{
public:
    enum
    {
        kRate = 0,
        kDepth = 1,
        kCentreFreq = 2,
        kFeedback = 3
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    juce::dsp::Phaser<float> phaser;
};

class EqModule final : public EffectModule
{
public:
    enum
    {
        kLowGainDb = 0,
        kMidGainDb = 1,
        kMidFreq = 2,
        kHighGainDb = 3
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    double rate = kDefaultSampleRate;
    Biquad low[2], mid[2], high[2];
};

/** Dynamics. juce::dsp::Compressor is hard-knee and reports no gain reduction,
    which is what it is: the makeup gain is applied here rather than asked of
    it, because it does not have one. */
class CompressorModule final : public EffectModule
{
public:
    enum
    {
        kThreshold = 0,
        kRatio = 1,
        kAttackMs = 2,
        kReleaseMs = 3,
        kMakeup = 4
    };

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::dsp::Compressor<float> compressor;
};

/** A brick wall at the ceiling: transparent below it, held at it above.

    Built on juce::dsp::Compressor rather than juce::dsp::Limiter, which is a
    MAXIMISER - it normalises its output back to full scale and adds 3.7dB of
    fixed makeup on top, so a signal already under the threshold comes out
    louder than it went in and the threshold is not a ceiling at all. Naming
    that parameter `ceiling` and then handing it to that widget would have been
    a label describing something the code does not do.

    So: the same second stage juce::dsp::Limiter uses - ratio 1000, attack as
    near zero as the ballistics allow - and a hard clip at the ceiling for the
    overshoot no finite attack can catch.
*/
class LimiterModule final : public EffectModule
{
public:
    enum
    {
        kCeiling = 0,
        kReleaseMs = 1
    };

    /** What "as fast as possible" is, in milliseconds, and the ratio at which a
        compressor stops being one. Both are juce::dsp::Limiter's own numbers. */
    static constexpr float attackMs = 0.001f;
    static constexpr float ratio = 1000.0f;

    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void process (ParamBlock, StereoView) noexcept override;

private:
    juce::dsp::Compressor<float> limiter;
};

} // namespace dew
