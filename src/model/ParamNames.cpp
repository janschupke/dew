// =============================================================================
// What a parameter is CALLED, as a declared table.
//
// Its own translation unit beside ParamRole.cpp, and for the same reason: the
// catalog declares what a parameter IS, and these declare what it is FOR and
// what it is CALLED. Only the property identifier joins them.
//
// The names themselves are not here. They are keys into resources/i18n/en.json,
// because a caption is a thing a person reads and every one of those lives in
// the catalogue.
// =============================================================================

#include "model/ParamNames.h"

#include "model/Ids.h"

namespace dew
{

namespace
{

struct NameRow
{
    const juce::Identifier* property;
    StringId name;
    StringId caption;
};

/** Every parameter dew has, said once, by what it is called.

    Exhaustive over the catalog, and ParamNameTests holds it that way - a
    parameter added without a row here would otherwise fall back to its own
    identifier and paint a knob with "wavePositionRate".
*/
// clang-format off
const NameRow nameRows[] {
    { &ids::attack, StringId::param_attack_name, StringId::param_attack_caption },
    { &ids::attackMs, StringId::param_attackMs_name, StringId::param_attackMs_caption },
    { &ids::attackScale, StringId::param_attackScale_name, StringId::param_attackScale_caption },
    { &ids::basePitch, StringId::param_basePitch_name, StringId::param_basePitch_caption },
    { &ids::ceiling, StringId::param_ceiling_name, StringId::param_ceiling_caption },
    { &ids::centreFreq, StringId::param_centreFreq_name, StringId::param_centreFreq_caption },
    { &ids::cutoff, StringId::param_cutoff_name, StringId::param_cutoff_caption },
    { &ids::damping, StringId::param_damping_name, StringId::param_damping_caption },
    { &ids::decay, StringId::param_decay_name, StringId::param_decay_caption },
    { &ids::delayMs, StringId::param_delayMs_name, StringId::param_delayMs_caption },
    { &ids::depth, StringId::param_depth_name, StringId::param_depth_caption },
    { &ids::detuneCents, StringId::param_detuneCents_name, StringId::param_detuneCents_caption },
    { &ids::distortionMode, StringId::param_distortionMode_name, StringId::param_distortionMode_caption },
    { &ids::drive, StringId::param_drive_name, StringId::param_drive_caption },
    { &ids::enabled, StringId::param_enabled_name, StringId::param_enabled_caption },
    { &ids::fadeInMs, StringId::param_fadeInMs_name, StringId::param_fadeInMs_caption },
    { &ids::fadeOutMs, StringId::param_fadeOutMs_name, StringId::param_fadeOutMs_caption },
    { &ids::feedback, StringId::param_feedback_name, StringId::param_feedback_caption },
    { &ids::filterMode, StringId::param_filterMode_name, StringId::param_filterMode_caption },
    { &ids::filterOffset, StringId::param_filterOffset_name, StringId::param_filterOffset_caption },
    { &ids::gain, StringId::param_gain_name, StringId::param_gain_caption },
    { &ids::highGainDb, StringId::param_highGainDb_name, StringId::param_highGainDb_caption },
    { &ids::loop, StringId::param_loop_name, StringId::param_loop_caption },
    { &ids::lowGainDb, StringId::param_lowGainDb_name, StringId::param_lowGainDb_caption },
    { &ids::makeup, StringId::param_makeup_name, StringId::param_makeup_caption },
    { &ids::midFreq, StringId::param_midFreq_name, StringId::param_midFreq_caption },
    { &ids::midGainDb, StringId::param_midGainDb_name, StringId::param_midGainDb_caption },
    { &ids::mix, StringId::param_mix_name, StringId::param_mix_caption },
    { &ids::mode, StringId::param_mode_name, StringId::param_mode_caption },
    { &ids::mute, StringId::param_mute_name, StringId::param_mute_caption },
    { &ids::muted, StringId::param_muted_name, StringId::param_muted_caption },
    { &ids::octave, StringId::param_octave_name, StringId::param_octave_caption },
    { &ids::outputGain, StringId::param_outputGain_name, StringId::param_outputGain_caption },
    { &ids::pan, StringId::param_pan_name, StringId::param_pan_caption },
    { &ids::rate, StringId::param_rate_name, StringId::param_rate_caption },
    { &ids::ratio, StringId::param_ratio_name, StringId::param_ratio_caption },
    { &ids::release, StringId::param_release_name, StringId::param_release_caption },
    { &ids::releaseMs, StringId::param_releaseMs_name, StringId::param_releaseMs_caption },
    { &ids::releaseScale, StringId::param_releaseScale_name, StringId::param_releaseScale_caption },
    { &ids::resonance, StringId::param_resonance_name, StringId::param_resonance_caption },
    { &ids::reverse, StringId::param_reverse_name, StringId::param_reverse_caption },
    { &ids::roomSize, StringId::param_roomSize_name, StringId::param_roomSize_caption },
    { &ids::sustain, StringId::param_sustain_name, StringId::param_sustain_caption },
    { &ids::tempoBpm, StringId::param_tempoBpm_name, StringId::param_tempoBpm_caption },
    { &ids::threshold, StringId::param_threshold_name, StringId::param_threshold_caption },
    { &ids::tone, StringId::param_tone_name, StringId::param_tone_caption },
    { &ids::transpose, StringId::param_transpose_name, StringId::param_transpose_caption },
    { &ids::tuneCents, StringId::param_tuneCents_name, StringId::param_tuneCents_caption },
    { &ids::unisonDetune, StringId::param_unisonDetune_name, StringId::param_unisonDetune_caption },
    { &ids::unisonVoices, StringId::param_unisonVoices_name, StringId::param_unisonVoices_caption },
    { &ids::velocitySens, StringId::param_velocitySens_name, StringId::param_velocitySens_caption },
    { &ids::volume, StringId::param_volume_name, StringId::param_volume_caption },
    { &ids::wave, StringId::param_wave_name, StringId::param_wave_caption },
    { &ids::wavePosition, StringId::param_wavePosition_name, StringId::param_wavePosition_caption },
    { &ids::wavePositionMod, StringId::param_wavePositionMod_name, StringId::param_wavePositionMod_caption },
    { &ids::wavePositionRate, StringId::param_wavePositionRate_name, StringId::param_wavePositionRate_caption },
    { &ids::wavePositionSource, StringId::param_wavePositionSource_name, StringId::param_wavePositionSource_caption },
    { &ids::wavetable, StringId::param_wavetable_name, StringId::param_wavetable_caption },
    { &ids::width, StringId::param_width_name, StringId::param_width_caption },
};
// clang-format on

const NameRow* rowFor (const juce::Identifier& property) noexcept
{
    for (const auto& row : nameRows)
        if (*row.property == property)
            return &row;

    return nullptr;
}

} // namespace

StringId paramNameOf (const juce::Identifier& property) noexcept
{
    // A parameter with no row is a defect the test catches, but it reaches a
    // knob before it reaches the test - so the fallback is the one string that
    // is never empty and never wrong about what it is naming.
    if (const auto* row = rowFor (property))
        return row->name;

    return StringId::param_unknown_name;
}

StringId paramCaptionOf (const juce::Identifier& property) noexcept
{
    if (const auto* row = rowFor (property))
        return row->caption;

    return StringId::param_unknown_caption;
}

juce::Array<const juce::Identifier*> namedParameters()
{
    juce::Array<const juce::Identifier*> properties;

    for (const auto& row : nameRows)
        properties.add (row.property);

    return properties;
}

} // namespace dew
