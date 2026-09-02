#include "model/ModuleCatalog.h"

#include "model/Ids.h"

namespace dew
{

namespace
{

constexpr ParamChoice filterModes[] {
    { "lowpass",  "Low pass"  },
    { "highpass", "High pass" },
    { "bandpass", "Band pass" },
};

// Ranges reconciled to the widest the engine can actually render, so there is
// one answer rather than one per table. The only effect parameter where the
// four tables disagreed was `cutoff`: the engine loaded it up to 20kHz while
// the automation table and the knob both stopped at 18kHz, which was an
// arbitrary number and cost the filter its top octave.

const ParamSpec commonParams[] {
    { &ids::mix, "Mix", "MIX", "", 0.0, 1.0, 1.0, 0.01, 2 },
};

const ParamSpec filterParams[] {
    { &ids::filterMode, "Mode", "MODE", "", 0.0, 0.0, 0.0, 0.0, 0,
      ParamCurve::linear, ParamControl::choice, false, /*automatable*/ false, false,
      filterModes, (int) std::size (filterModes), "lowpass" },
    { &ids::cutoff, "Cutoff", "CUTOFF", " Hz", 20.0, 20000.0, 1200.0, 1.0, 0,
      ParamCurve::logarithmic, ParamControl::field },
    { &ids::resonance, "Resonance", "RES", "", 0.05, 4.0, 0.4, 0.01, 2 },
};

const ParamSpec reverbParams[] {
    { &ids::roomSize, "Size",    "SIZE",  "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::damping,  "Damping", "DAMP",  "", 0.0, 1.0, 0.5, 0.01, 2 },
    { &ids::width,    "Width",   "WIDTH", "", 0.0, 1.0, 1.0, 0.01, 2 },
};

const ParamSpec delayParams[] {
    { &ids::delayMs,  "Time",     "TIME", " ms", 1.0, 1000.0, 250.0, 1.0, 0,
      ParamCurve::logarithmic, ParamControl::field },
    { &ids::feedback, "Feedback", "FBK",  "", 0.0, 0.95, 0.35, 0.01, 2 },
};

const ParamSpec driveParams[] {
    { &ids::drive,      "Drive",  "DRIVE", "", 1.0, 40.0, 2.0, 0.1,  1 },
    { &ids::outputGain, "Output", "OUT",   "", 0.0, 4.0,  1.0, 0.01, 2 },
};

const ParamSpec chorusParams[] {
    { &ids::rate,  "Rate",  "RATE",  " Hz", 0.01, 20.0, 1.2, 0.01, 2,
      ParamCurve::logarithmic, ParamControl::field },
    { &ids::depth, "Depth", "DEPTH", "", 0.0, 1.0, 0.3, 0.01, 2 },
};

const ParamSpec eqParams[] {
    { &ids::lowGainDb,  "Low",  "LOW",  " dB", -24.0, 24.0, 0.0, 0.1, 1,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true },
    { &ids::midGainDb,  "Mid",  "MID",  " dB", -24.0, 24.0, 0.0, 0.1, 1,
      ParamCurve::linear, ParamControl::knob, true },
    { &ids::midFreq,    "Freq", "FREQ", " Hz", 100.0, 8000.0, 900.0, 1.0, 0,
      ParamCurve::logarithmic, ParamControl::field },
    { &ids::highGainDb, "High", "HIGH", " dB", -24.0, 24.0, 0.0, 0.1, 1,
      ParamCurve::linear, ParamControl::knob, true },
};

} // namespace

const std::vector<EffectDescriptor>& effectDescriptors()
{
    static const std::vector<EffectDescriptor> all {
        { EffectType::filter, "filter", "Filter", filterParams, (int) std::size (filterParams) },
        { EffectType::reverb, "reverb", "Reverb", reverbParams, (int) std::size (reverbParams) },
        { EffectType::delay,  "delay",  "Delay",  delayParams,  (int) std::size (delayParams)  },
        { EffectType::drive,  "drive",  "Drive",  driveParams,  (int) std::size (driveParams)  },
        { EffectType::chorus, "chorus", "Chorus", chorusParams, (int) std::size (chorusParams) },
        { EffectType::eq,     "eq",     "EQ",     eqParams,     (int) std::size (eqParams)     },
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
    return effectDescriptor (type).displayName;
}

const std::vector<ParamSpec>& commonEffectParams()
{
    static const std::vector<ParamSpec> common { std::begin (commonParams),
                                                 std::end (commonParams) };
    return common;
}

std::vector<ParamSpec> effectParamsFor (EffectType type)
{
    const auto& descriptor = effectDescriptor (type);

    std::vector<ParamSpec> params { descriptor.params, descriptor.params + descriptor.numParams };

    const auto& common = commonEffectParams();
    params.insert (params.end(), common.begin(), common.end());

    return params;
}

} // namespace dew
