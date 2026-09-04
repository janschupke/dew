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
    distortion,
    chorus,
    phaser,
    eq,
    compressor,
    limiter
};

inline constexpr int kNumEffectTypes = 10;

enum class FilterMode
{
    lowpass,
    highpass,
    bandpass
};

/** How a distortion slot shapes a sample.

    Separate from FilterMode rather than one shared enum, because the two name
    positions in different tables and a shared enum would let a filter's mode
    index a distortion's shaper with no complaint from anything.
*/
enum class DistortionMode
{
    softClip,
    hardClip,
    fold,
    crush
};

// clang-format on
FilterMode filterModeFromString (const juce::String&);
juce::String filterModeToString (FilterMode);

} // namespace dew
