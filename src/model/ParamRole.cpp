// =============================================================================
// What a parameter DOES, as a declared table.
//
// Its own translation unit rather than a section of ModuleCatalog.cpp, which
// the length gate is right about: the catalog declares what every parameter IS
// - its range, its curve, its control - and this declares what each one is FOR.
// They are read at different times by different people, and only the property
// identifier joins them.
// =============================================================================

#include "model/ParamRole.h"

#include "model/Ids.h"

namespace dew
{

namespace
{

struct RoleRow
{
    const juce::Identifier* property;
    ParamRole role;
};

/** Every parameter dew has, said once, by what it DOES.

    Grouped by role rather than by the table each identifier came from, because
    the grouping IS the claim: a soundfont's `filterOffset` sits beside a filter
    slot's `cutoff` here, and reading the two together is what makes it obvious
    they should not be different colours.

    Exhaustive over the catalog, and ParamRoleTests holds it that way - a
    parameter added without a row here fails by name rather than quietly
    painting itself in the generic accent.
*/
const RoleRow roleRows[] {
    // Multipurpose. `mix` is the parameter EVERY effect shares, which makes it
    // the generic case by definition; `tempoBpm` lives in the transport strip,
    // where the playhead amber already owns the colour. The rest are switches,
    // and a switch already says what it is by being on.
    { &ids::mix, ParamRole::generic },
    { &ids::tempoBpm, ParamRole::generic },
    { &ids::enabled, ParamRole::generic },
    { &ids::muted, ParamRole::generic },
    { &ids::mute, ParamRole::generic },
    { &ids::reverse, ParamRole::generic },
    { &ids::loop, ParamRole::generic },

    // How loud. `sustain` is here and not with the other envelope stages: it is
    // the one ADSR value that is an amplitude.
    { &ids::volume, ParamRole::level },
    { &ids::gain, ParamRole::level },
    { &ids::outputGain, ParamRole::level },
    { &ids::sustain, ParamRole::level },
    { &ids::velocitySens, ParamRole::level },
    { &ids::threshold, ParamRole::level },
    { &ids::ratio, ParamRole::level },
    { &ids::makeup, ParamRole::level },
    { &ids::ceiling, ParamRole::level },

    // Where in the field. A reverb's `width` is a stereo quantity, not a size.
    { &ids::pan, ParamRole::stereo },
    { &ids::width, ParamRole::stereo },

    // Spectral shaping. `drive` is here because distortion is timbre, and the
    // waveform choices are here because a waveform IS a spectrum.
    { &ids::filterMode, ParamRole::tone },
    { &ids::cutoff, ParamRole::tone },
    { &ids::resonance, ParamRole::tone },
    { &ids::filterOffset, ParamRole::tone },
    { &ids::damping, ParamRole::tone },
    { &ids::drive, ParamRole::tone },
    { &ids::lowGainDb, ParamRole::tone },
    { &ids::midGainDb, ParamRole::tone },
    { &ids::midFreq, ParamRole::tone },
    { &ids::highGainDb, ParamRole::tone },
    { &ids::wave, ParamRole::tone },
    { &ids::wavetable, ParamRole::tone },
    { &ids::mode, ParamRole::tone },
    { &ids::distortionMode, ParamRole::tone },
    { &ids::tone, ParamRole::tone },
    { &ids::centreFreq, ParamRole::tone },

    // Envelope in time. The soundfont's two are multipliers rather than times,
    // but they scale the same stages and belong with them.
    { &ids::attack, ParamRole::time },
    { &ids::decay, ParamRole::time },
    { &ids::release, ParamRole::time },
    { &ids::fadeInMs, ParamRole::time },
    { &ids::fadeOutMs, ParamRole::time },
    { &ids::attackScale, ParamRole::time },
    { &ids::releaseScale, ParamRole::time },
    { &ids::attackMs, ParamRole::time },
    { &ids::releaseMs, ParamRole::time },

    // Ambience and echo. A delay's time is a distance, not an envelope stage.
    { &ids::roomSize, ParamRole::space },
    { &ids::delayMs, ParamRole::space },
    { &ids::feedback, ParamRole::space },

    // What makes it move. `unisonDetune` is here and `detuneCents` is not: a
    // spread across voices thickens, a fixed offset transposes.
    { &ids::rate, ParamRole::modulation },
    { &ids::depth, ParamRole::modulation },
    { &ids::wavePosition, ParamRole::modulation },
    { &ids::wavePositionMod, ParamRole::modulation },
    { &ids::wavePositionSource, ParamRole::modulation },
    { &ids::wavePositionRate, ParamRole::modulation },
    { &ids::unisonVoices, ParamRole::modulation },
    { &ids::unisonDetune, ParamRole::modulation },

    // Which note you hear.
    { &ids::basePitch, ParamRole::pitch },
    { &ids::octave, ParamRole::pitch },
    { &ids::detuneCents, ParamRole::pitch },
    { &ids::transpose, ParamRole::pitch },
    { &ids::tuneCents, ParamRole::pitch },
};

} // namespace

std::optional<ParamRole> declaredRoleOf (const juce::Identifier& property) noexcept
{
    for (const auto& row : roleRows)
        if (*row.property == property)
            return row.role;

    return {};
}

ParamRole roleOf (const juce::Identifier& property) noexcept
{
    return declaredRoleOf (property).value_or (ParamRole::generic);
}

juce::StringArray declaredRoleNames()
{
    juce::StringArray names;

    for (const auto& row : roleRows)
        names.add (row.property->toString());

    return names;
}

ParamRole roleOfEffect (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::filter:
        case EffectType::eq:
        case EffectType::drive:
        case EffectType::distortion: return ParamRole::tone;

        case EffectType::reverb:
        case EffectType::delay: return ParamRole::space;

        case EffectType::chorus:
        case EffectType::phaser: return ParamRole::modulation;

        // Loudness devices, and the palette has no eighth hue far enough from
        // the seven it has to be worth splitting dynamics off level.
        case EffectType::compressor:
        case EffectType::limiter: return ParamRole::level;
    }

    // No default above, so a new effect type is a compiler warning here rather
    // than a card that silently paints itself generic.
    return ParamRole::generic;
}

} // namespace dew
