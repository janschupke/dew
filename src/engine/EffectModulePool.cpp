#include "engine/EffectModulePool.h"

#include "engine/ModuleFactory.h"

namespace dew
{

void EffectModulePool::prepare (double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : kDefaultSampleRate;
    blockSize = juce::jmax (1, maximumBlockSize);
    prepared = true;

    // Everything already made is re-prepared; nothing NEW is made here.
    // prepare() can arrive on the device thread, and making a module allocates.
    for (auto& module : owned)
        module->prepare (sampleRate, blockSize);
}

void EffectModulePool::releaseResources()
{
    for (auto& module : owned)
        module->releaseResources();

    prepared = false;
}

EffectModule* EffectModulePool::acquire (int poolIndex, EffectType type)
{
    for (const auto& entry : index)
        if (entry.poolIndex == poolIndex && entry.type == type)
            return entry.module;

    auto module = createEffectModule (type);

    if (module == nullptr)
        return nullptr;

    if (prepared)
        module->prepare (sampleRate, blockSize);

    auto* raw = module.get();
    owned.push_back (std::move (module));
    index.push_back ({ poolIndex, type, raw });

    return raw;
}

} // namespace dew
