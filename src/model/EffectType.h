#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** The effects a chain slot can hold. Ordered as they appear in the picker.

// clang-format off
    In the model rather than the engine because the schema, the automation
    picker and the effect editor all need to name a type, and none of them
    should have to link the DSP to do it.
*/
enum class EffectType
{
    filter,
    reverb,
    delay,
    drive,
    chorus,
    eq
};

inline constexpr int kNumEffectTypes = 6;

enum class FilterMode
{
    lowpass,
    highpass,
    bandpass
};

// clang-format on
FilterMode filterModeFromString (const juce::String&);
juce::String filterModeToString (FilterMode);

} // namespace dew
