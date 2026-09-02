#include "model/ModuleCatalog.h"

#include "model/Ids.h"
#include "model/ProjectSchema.h"

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

// --- the instrument's own parameters -----------------------------------------

namespace
{

const ParamSpec channelSpecs[] {
    { &ids::volume, "Volume", "VOLUME", "", 0.0, 1.0, 0.8, 0.001, 3 },
    { &ids::pan,    "Pan",    "PAN",    "", -1.0, 1.0, 0.0, 0.001, 3,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true },

    // A MIDI note number, so the range is the whole of MIDI and the value is
    // an integer in the file.
    { &ids::basePitch, "Base pitch", "PITCH", "", 0.0, 127.0, 60.0, 1.0, 0,
      ParamCurve::linear, ParamControl::stepper, false, /*automatable*/ false,
      /*integral*/ true },
};

const ParamSpec ampSpecs[] {
    // Ten seconds, which is what the engine renders. The knobs stopped at two
    // and four, so the top of the envelope was simply unreachable.
    //
    // Logarithmic, because linear puts every usable attack in the first one per
    // cent of the travel: half a millisecond to ten seconds is four and a half
    // decades, and a knob that spends nine tenths of its sweep between five and
    // ten seconds is a knob with one useful position.
    { &ids::attack,  "Attack",  "ATTACK",  " s", 0.0005, 10.0, 0.005, 0.0005, 4,
      ParamCurve::logarithmic },
    { &ids::decay,   "Decay",   "DECAY",   " s", 0.0005, 10.0, 0.120, 0.0005, 4,
      ParamCurve::logarithmic },
    { &ids::sustain, "Sustain", "SUSTAIN", "",   0.0,    1.0,  0.700, 0.001,  3 },
    { &ids::release, "Release", "RELEASE", " s", 0.002,  10.0, 0.150, 0.001,  3,
      ParamCurve::logarithmic },
};

const ParamSpec oscSpecs[] {
    // Four octaves either way, which is what the engine renders; the stepper
    // offered three.
    { &ids::octave, "Octave", "OCT", "", -4.0, 4.0, 0.0, 1.0, 0,
      ParamCurve::linear, ParamControl::stepper, true, /*automatable*/ false,
      /*integral*/ true },

    // One semitone either way, and here the KNOB is the one that wins. The
    // engine clamps at twelve semitones, but that is what `octave` is for, and
    // a control covering two octaves cannot be nudged by a cent - which is the
    // only thing anyone detunes an oscillator by.
    { &ids::detuneCents, "Detune", "DETUNE", " c", -100.0, 100.0, 0.0, 1.0, 0,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true, /*automatable*/ false,
      /*integral*/ true },

    { &ids::gain, "Gain", "GAIN", "", 0.0, 1.0, 0.8, 0.01, 2 },

    { &ids::wavePosition,     "Position", "POSITION", "",    0.0,  1.0,  0.0, 0.01, 2 },
    { &ids::wavePositionMod,  "Mod",      "MOD",      "",   -1.0,  1.0,  0.0, 0.01, 2,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true },
    { &ids::wavePositionRate, "Rate",     "RATE",     " Hz", 0.01, 20.0, 1.0, 0.01, 2,
      ParamCurve::logarithmic },

    { &ids::unisonVoices,     "Unison",   "UNISON",   "",    1.0,  (double) kMaxUnisonVoices,
      1.0, 1.0, 0, ParamCurve::linear, ParamControl::knob, false, /*automatable*/ false,
      /*integral*/ true },
    { &ids::unisonDetune,     "Spread",   "SPREAD",   " c",  0.0, 50.0, 0.0, 0.5, 1 },
};

const ParamSpec mixerTrackSpecs[] {
    // The three-way disagreement the plan named: the fader offered 0..1.5, the
    // engine clamped at 2.0 and automation mapped onto 0..1, so automating a
    // fader reached two thirds of its travel and the top third of the engine's
    // range was unreachable from anywhere. The FADER wins: 2.0 is six decibels
    // no control ever offered.
    { &ids::gain, "Gain", "GAIN", "", 0.0, 1.5, 0.8, 0.001, 3 },
    { &ids::pan,  "Pan",  "PAN",  "", -1.0, 1.0, 0.0, 0.001, 3,
      ParamCurve::linear, ParamControl::knob, /*bipolar*/ true },
};

template <size_t N>
const std::vector<ParamSpec>& asVector (const ParamSpec (&table)[N])
{
    static const std::vector<ParamSpec> specs { std::begin (table), std::end (table) };
    return specs;
}

} // namespace

const std::vector<ParamSpec>& channelParamSpecs() { return asVector (channelSpecs); }
const std::vector<ParamSpec>& ampParamSpecs()     { return asVector (ampSpecs); }
const std::vector<ParamSpec>& oscParamSpecs()     { return asVector (oscSpecs); }
const std::vector<ParamSpec>& mixerTrackParamSpecs() { return asVector (mixerTrackSpecs); }

const ParamSpec* instrumentParamSpec (const juce::Identifier& property) noexcept
{
    // Order matters where two tables name the same property. `gain` is both an
    // oscillator's level and a mixer track's fader, and `pan` is both a
    // channel's and a track's; the instrument tables are searched first
    // because this is the INSTRUMENT lookup, and the mixer asks for its own.
    for (const auto* table : { &channelParamSpecs(), &ampParamSpecs(), &oscParamSpecs() })
        for (const auto& spec : *table)
            if (*spec.property == property)
                return &spec;

    return nullptr;
}

const ParamSpec* mixerTrackParamSpec (const juce::Identifier& property) noexcept
{
    for (const auto& spec : mixerTrackParamSpecs())
        if (*spec.property == property)
            return &spec;

    return nullptr;
}

const ParamSpec& requireMixerTrackParamSpec (const juce::Identifier& property)
{
    const auto* spec = mixerTrackParamSpec (property);
    jassert (spec != nullptr);

    static const ParamSpec fallback {};
    return spec != nullptr ? *spec : fallback;
}

const ParamSpec& requireInstrumentParamSpec (const juce::Identifier& property)
{
    const auto* spec = instrumentParamSpec (property);

    // A caller asking for a spec that does not exist has a bug in the table,
    // not in the document, and a default-shaped fallback would hide it behind a
    // knob that silently spans nought to one.
    jassert (spec != nullptr);

    static const ParamSpec fallback {};
    return spec != nullptr ? *spec : fallback;
}

} // namespace dew
