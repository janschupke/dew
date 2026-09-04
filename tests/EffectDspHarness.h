#pragma once

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/Effects.h"
#include "engine/ModuleFactory.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"

/** Driving one effect module directly, with no engine and no project behind it.

    Shared by the DSP tests and the module-pool tests: both build a module,
    push a buffer through it and measure what came out, and a second copy of
    that would be a second idea of what "the same signal" means.
*/
namespace dew::testing
{

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;

/** One effect's parameters, named rather than indexed.

    The block the engine passes a module is positional, which is right for the
    audio thread and unreadable in a test. This names them through the catalog,
    so a test still says `.set (ids::cutoff, 200.0f)` and a reordered descriptor
    moves the value with it rather than silently driving the neighbour.
*/
struct Params
{
    explicit Params (EffectType t)
        : type (t)
    {
        // Start from the declared defaults, so a test only states what it cares
        // about - exactly as `EffectParams params;` used to.
        for (const auto& spec : effectParamsFor (type))
        {
            const auto index = effectParamIndex (type, *spec.property);

            if (index < 0)
                continue;

            if (spec.control == ParamControl::choice)
            {
                for (int i = 0; i < spec.numChoices; ++i)
                    if (juce::String (spec.choices[i].id) == spec.defaultText)
                        block[(size_t) index] = (float) i;
            }
            else
            {
                block[(size_t) index] = (float) spec.defaultValue;
            }
        }
    }

    Params& set (const juce::Identifier& property, float value)
    {
        const auto index = effectParamIndex (type, property);
        REQUIRE (index >= 0);
        block[(size_t) index] = value;
        return *this;
    }

    Params& setMode (FilterMode mode)
    {
        return set (ids::filterMode, (float) mode);
    }

    EffectType type;
    EffectParamBlock block {};
};

/** Runs a signal through one effect and hands back the result.

    Through processEffectSlot, which is the same function the engine's chain
    runner calls. A test that applied its own dry/wet would pin its own
    arithmetic rather than the engine's - and the fully-dry passthrough below is
    exactly the assertion that would then prove nothing.
*/
inline void runEffect (const Params& params, juce::AudioBuffer<float>& buffer)
{
    auto module = createEffectModule (params.type);
    REQUIRE (module != nullptr);

    module->prepare (sampleRate, blockSize);

    juce::AudioBuffer<float> dryScratch (2, blockSize);

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto n = juce::jmin (blockSize, buffer.getNumSamples() - start);
        processEffectSlot (
            *module, params.block, params.type,
            { buffer.getWritePointer (0) + start, buffer.getWritePointer (1) + start, n },
            dryScratch);
    }
}

inline juce::AudioBuffer<float> sineBuffer (double frequency, int numSamples,
                                            float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer (2, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto value = amplitude
                           * (float) std::sin (juce::MathConstants<double>::twoPi * frequency
                                               * (double) i / sampleRate);
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }

    return buffer;
}

/** RMS of a window, which is how "did this get quieter" is actually measured. */
inline float rmsOf (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    const auto n = juce::jmin (length, buffer.getNumSamples() - start);
    return n > 0 ? buffer.getRMSLevel (0, start, n) : 0.0f;
}

} // namespace dew::testing
