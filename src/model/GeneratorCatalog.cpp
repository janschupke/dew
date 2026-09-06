#include "model/GeneratorCatalog.h"

#include "model/Ids.h"
#include "model/InstrumentType.h"
#include "model/ModuleCatalog.h"

namespace dew
{

namespace
{

/** Every group whose node hangs UNDER an oscillator slot: the two generators,
    and the LFO.

    Read off the synth's own descriptor rather than listed here, so a fourth
    such node is a row in ModuleCatalog and nothing at all in this file. Which
    is the point - the alternative is that "what lives under a slot" is written
    down twice and the two drift.
*/
const std::vector<const ParamGroup*>& oscChildGroups()
{
    static const std::vector<const ParamGroup*> groups = []
    {
        std::vector<const ParamGroup*> found;
        const auto& synth = instrumentDescriptor (InstrumentType::synth);

        for (int i = 0; i < synth.numGroups; ++i)
            if (synth.groups[i].under != nullptr && *synth.groups[i].under == ids::OSC)
                found.push_back (&synth.groups[i]);

        return found;
    }();

    return groups;
}

} // namespace

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

    // The LFO's, whichever generator this is - it is the slot's, not a
    // generator's. Leaving it out here would be silent: this is what
    // oscParams() walks, so every LFO parameter would be declared automatable,
    // have an enumerator and an engine that applies it, and simply never be
    // offered by the picker.
    const auto& lfo = oscLfoParamSpecs();
    all.insert (all.end(), lfo.begin(), lfo.end());

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

bool isOscChildNode (const juce::ValueTree& node)
{
    for (const auto* group : oscChildGroups())
        if (node.hasType (*group->node))
            return true;

    return false;
}

juce::ValueTree generatorNodeFor (const juce::ValueTree& slot, const juce::Identifier& property)
{
    for (const auto* group : oscChildGroups())
        for (int i = 0; i < group->numParams; ++i)
            if (*group->params[i].property == property)
                return slot.getChildWithName (*group->node);

    // One of the slot's own - whether it is on, its octave, its detune, its
    // gain, which generator it runs.
    return slot;
}

} // namespace dew
