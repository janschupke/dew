#include "engine/ModuleFactory.h"

#include "engine/modules/EffectModules.h"

namespace dew
{

std::unique_ptr<EffectModule> createEffectModule (EffectType type)
{
    switch (type)
    {
        case EffectType::filter: return std::make_unique<FilterModule>();
        case EffectType::reverb: return std::make_unique<ReverbModule>();
        case EffectType::delay:  return std::make_unique<DelayModule>();
        case EffectType::drive:  return std::make_unique<DriveModule>();
        case EffectType::chorus: return std::make_unique<ChorusModule>();
        case EffectType::eq:     return std::make_unique<EqModule>();
    }

    return {};
}

void processEffectSlot (EffectModule& module, const EffectParamBlock& params, EffectType type,
                        StereoView io, juce::AudioBuffer<float>& dryScratch) noexcept
{
    const auto mix = juce::jlimit (0.0f, 1.0f, params[(size_t) kMixParamIndex]);

    // Fully dry is not "process then blend to nothing": it is untouched. The
    // difference is bit-exactness, and a test pins it.
    if (mix <= 0.0f)
        return;

    const auto keepDry = mix < 1.0f
                      && dryScratch.getNumChannels() >= 2
                      && io.numSamples <= dryScratch.getNumSamples();

    if (keepDry)
    {
        juce::FloatVectorOperations::copy (dryScratch.getWritePointer (0), io.left, io.numSamples);
        juce::FloatVectorOperations::copy (dryScratch.getWritePointer (1), io.right, io.numSamples);
    }

    module.process ({ params.data() + kNumCommonEffectParams, effectDescriptor (type).numParams },
                    io);

    if (keepDry)
    {
        const auto dry = 1.0f - mix;

        juce::FloatVectorOperations::multiply (io.left, mix, io.numSamples);
        juce::FloatVectorOperations::multiply (io.right, mix, io.numSamples);
        juce::FloatVectorOperations::addWithMultiply (io.left, dryScratch.getReadPointer (0),
                                                      dry, io.numSamples);
        juce::FloatVectorOperations::addWithMultiply (io.right, dryScratch.getReadPointer (1),
                                                      dry, io.numSamples);
    }
}

} // namespace dew
