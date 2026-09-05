#include "model/GeneratorCatalog.h"

#include "model/ModuleCatalog.h"

namespace dew
{

std::optional<int> generatorIndexFor (juce::StringRef id)
{
    const auto& all = generatorDescriptors();

    for (size_t i = 0; i < all.size(); ++i)
        if (juce::String (all[i].id) == id)
            return (int) i;

    return {};
}

const GeneratorDescriptor& generatorFor (juce::StringRef id)
{
    const auto& all = generatorDescriptors();

    // The FIRST, which is classic - what a slot with no opinion has always
    // been, and what a file naming a generator this build does not have should
    // still make a sound as.
    return all[(size_t) generatorIndexFor (id).value_or (0)];
}

std::vector<ParamSpec> generatorParamSpecs (juce::StringRef id)
{
    const auto& generator = generatorFor (id);
    const auto& shared = oscSlotParamSpecs();

    std::vector<ParamSpec> all { shared.begin(), shared.end() };

    for (int i = 0; i < generator.numParams; ++i)
        all.push_back (generator.params[i]);

    return all;
}

bool isForeignGeneratorParam (juce::StringRef id, const juce::Identifier& property)
{
    const auto& mine = generatorFor (id);

    for (const auto& generator : generatorDescriptors())
    {
        if (juce::String (generator.id) == juce::String (mine.id))
            continue;

        for (int i = 0; i < generator.numParams; ++i)
            if (*generator.params[i].property == property)
                return true;
    }

    return false;
}

juce::ValueTree generatorNodeFor (const juce::ValueTree& slot, const juce::Identifier& property)
{
    for (const auto& generator : generatorDescriptors())
        for (int i = 0; i < generator.numParams; ++i)
            if (*generator.params[i].property == property)
                return slot.getChildWithName (*generator.node);

    // One of the slot's own - whether it is on, its octave, its detune, its
    // gain, which generator it runs.
    return slot;
}

} // namespace dew
