// =============================================================================
// The effect half of the catalog.
//
// Its own translation unit rather than a section of ModuleCatalog.cpp, which
// the 400-line gate was right to force. The header already said the two halves
// were twins: an effect is one node with a flat list of parameters, an
// instrument is a channel, three oscillator slots and an envelope. They are
// read by different people at different times, and the only thing they share is
// the shape of the descriptor.
//
// Ranges are reconciled to the widest the engine can actually render, so there
// is one answer rather than one per table.
// =============================================================================

#include "model/Ids.h"
#include "model/ModuleCatalog.h"

namespace dew
{

namespace
{

constexpr ParamChoice filterModes[] {
    { "lowpass", StringId::choice_filterMode_lowpass },
    { "highpass", StringId::choice_filterMode_highpass },
    { "bandpass", StringId::choice_filterMode_bandpass },
};

const ParamSpec commonParams[] {
    { &ids::mix, "", 0.0, 1.0, 1.0, 0.01, 2 },
};

/** The last mode's index, so the automation range is derived from the table
    rather than being a 2 that has to be remembered when a mode is added. */
constexpr double lastFilterMode = (double) (std::size (filterModes) - 1);

const ParamSpec filterParams[] {
    // A range of 0..lastFilterMode and integral, so an automation curve over it
    // is a curve over the choice INDEX - which is what a choice already is in the
    // slot's parameter block, and what every plugin API makes a discrete
    // parameter. Automatable now: it needed no engine plumbing at all, and it is
    // the proof that a stepped curve and a continuous one are one model.
    { &ids::filterMode, "", 0.0, lastFilterMode, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::choice, false, /*automatable*/ true, /*integral*/ true, filterModes,
      (int) std::size (filterModes), "lowpass" },
    { &ids::cutoff, " Hz", 20.0, 20000.0, 1200.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::resonance, "", 0.05, 4.0, 0.4, 0.01, 2 },
};

const ParamSpec reverbParams[] {
    { &ids::roomSize, "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::damping, "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::width, "", 0.0, 1.0, 1.0, 0.01, 2 },
};

const ParamSpec delayParams[] {
    { &ids::delayMs, " ms", 1.0, 1000.0, 250.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::feedback, "", 0.0, 0.95, 0.35, 0.01, 2 },
};

const ParamSpec driveParams[] {
    { &ids::drive, "", 1.0, 40.0, 2.0, 0.1, 1 },
    { &ids::outputGain, "", 0.0, 4.0, 1.0, 0.01, 2 },
};

/** Distortion's shapers. `drive` and `outputGain` are reused from Drive at
    exactly its ranges and defaults, because one EFFECT node carries every
    type's parameters and a reused identifier that disagreed about its default
    would silently take whichever type the schema saw first. */
constexpr ParamChoice distortionModes[] {
    { "softClip", StringId::choice_distortionMode_softClip },
    { "hardClip", StringId::choice_distortionMode_hardClip },
    { "fold", StringId::choice_distortionMode_fold },
    { "crush", StringId::choice_distortionMode_crush },
};

constexpr double lastDistortionMode = (double) (std::size (distortionModes) - 1);

const ParamSpec distortionParams[] {
    { &ids::distortionMode, "", 0.0, lastDistortionMode, 0.0, 1.0, 0, ParamCurve::linear,
      ParamControl::choice, false, /*automatable*/ true, /*integral*/ true, distortionModes,
      (int) std::size (distortionModes), "softClip" },
    { &ids::drive, "", 1.0, 40.0, 2.0, 0.1, 1 },
    { &ids::tone, "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::outputGain, "", 0.0, 4.0, 1.0, 0.01, 2 },
};

const ParamSpec chorusParams[] {
    { &ids::rate, " Hz", 0.01, 20.0, 1.2, 0.01, 2, ParamCurve::logarithmic, ParamControl::field },
    { &ids::depth, "", 0.0, 1.0, 0.3, 0.01, 2 },
};

/** The phaser reuses the chorus's `rate` and `depth` and the delay's
    `feedback`, all three at their existing ranges - which is also what makes the
    three modulation effects read as one family rather than three dialects. */
const ParamSpec phaserParams[] {
    { &ids::rate, " Hz", 0.01, 20.0, 1.2, 0.01, 2, ParamCurve::logarithmic, ParamControl::field },
    { &ids::depth, "", 0.0, 1.0, 0.3, 0.01, 2 },
    { &ids::centreFreq, " Hz", 20.0, 12000.0, 600.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::feedback, "", 0.0, 0.95, 0.35, 0.01, 2 },
};

/** The dynamics pair. `releaseMs` is declared once here and reused by the
    limiter at the same range: they are the same quantity doing the same job, so
    sharing it costs nothing and automating one reads the same as the other. */
const ParamSpec compressorParams[] {
    { &ids::threshold, " dB", -60.0, 0.0, -18.0, 0.1, 1 },
    { &ids::ratio, ":1", 1.0, 20.0, 4.0, 0.1, 1 },
    { &ids::attackMs, " ms", 0.1, 200.0, 10.0, 0.1, 1, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::releaseMs, " ms", 5.0, 1000.0, 100.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::makeup, " dB", 0.0, 24.0, 0.0, 0.1, 1 },
};

const ParamSpec limiterParams[] {
    { &ids::ceiling, " dB", -24.0, 0.0, -0.3, 0.1, 1 },
    { &ids::releaseMs, " ms", 5.0, 1000.0, 100.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
};

const ParamSpec eqParams[] {
    { &ids::lowGainDb, " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear, ParamControl::knob,
      /*bipolar*/ true },
    { &ids::midGainDb, " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear, ParamControl::knob,
      true },
    { &ids::midFreq, " Hz", 100.0, 8000.0, 900.0, 1.0, 0, ParamCurve::logarithmic,
      ParamControl::field },
    { &ids::highGainDb, " dB", -24.0, 24.0, 0.0, 0.1, 1, ParamCurve::linear, ParamControl::knob,
      true },
};

} // namespace

const std::vector<EffectDescriptor>& effectDescriptors()
{
    static const std::vector<EffectDescriptor> all {
        { EffectType::filter, "filter", StringId::effect_filter_name, filterParams,
          (int) std::size (filterParams) },
        { EffectType::reverb, "reverb", StringId::effect_reverb_name, reverbParams,
          (int) std::size (reverbParams) },
        { EffectType::delay, "delay", StringId::effect_delay_name, delayParams,
          (int) std::size (delayParams) },
        { EffectType::drive, "drive", StringId::effect_drive_name, driveParams,
          (int) std::size (driveParams) },
        { EffectType::distortion, "distortion", StringId::effect_distortion_name, distortionParams,
          (int) std::size (distortionParams) },
        { EffectType::chorus, "chorus", StringId::effect_chorus_name, chorusParams,
          (int) std::size (chorusParams) },
        { EffectType::phaser, "phaser", StringId::effect_phaser_name, phaserParams,
          (int) std::size (phaserParams) },
        { EffectType::eq, "eq", StringId::effect_eq_name, eqParams, (int) std::size (eqParams) },
        { EffectType::compressor, "compressor", StringId::effect_compressor_name, compressorParams,
          (int) std::size (compressorParams) },
        { EffectType::limiter, "limiter", StringId::effect_limiter_name, limiterParams,
          (int) std::size (limiterParams) },
    };

    // Not a comment asking someone to keep two things in step: adding an
    // enumerator without a row here stops the program before it starts.
    jassert ((int) all.size() == kNumEffectTypes);

    return all;
}

const EffectDescriptor& effectDescriptor (EffectType type) noexcept
{
    const auto& all = effectDescriptors();
    const auto index = (size_t) type;

    jassert (index < all.size());
    return all[juce::jmin (index, all.size() - 1)];
}

std::optional<EffectType> effectTypeFor (juce::StringRef id)
{
    for (const auto& descriptor : effectDescriptors())
        if (juce::String (descriptor.id) == id)
            return descriptor.type;

    return {};
}

juce::String effectTypeToString (EffectType type)
{
    return effectDescriptor (type).id;
}

juce::String effectTypeDisplayName (EffectType type)
{
    return tr (effectDescriptor (type).displayName);
}

const std::vector<ParamSpec>& commonEffectParams()
{
    static const std::vector<ParamSpec> common { std::begin (commonParams),
                                                 std::end (commonParams) };
    return common;
}

int effectParamIndex (EffectType type, const juce::Identifier& property) noexcept
{
    for (const auto& param : commonEffectParams())
        if (*param.property == property)
            return 0;

    const auto& descriptor = effectDescriptor (type);

    for (int i = 0; i < descriptor.numParams; ++i)
        if (*descriptor.params[i].property == property)
            return kNumCommonEffectParams + i;

    return -1;
}

std::vector<ParamSpec> effectParamsFor (EffectType type)
{
    const auto& descriptor = effectDescriptor (type);

    std::vector<ParamSpec> params { descriptor.params, descriptor.params + descriptor.numParams };

    const auto& common = commonEffectParams();
    params.insert (params.end(), common.begin(), common.end());

    return params;
}

ParamGroup effectGroup (EffectType type) noexcept
{
    // An effect is the degenerate instrument: one node, every parameter on it.
    // Saying so here is what lets the state functions walk both descriptors
    // with one loop body rather than two that must agree.
    const auto& descriptor = effectDescriptor (type);

    return { &ids::EFFECT, "effects", descriptor.displayName, descriptor.params,
             descriptor.numParams };
}

} // namespace dew
