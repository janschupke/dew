#include "model/PresetLibrary.h"

#include <PresetData.h>

#include "model/PresetFactory.h"
#include "model/PresetSerializer.h"

namespace dew
{

juce::String PresetLibrary::jsonFor (const juce::String& fileName)
{
    // juce_add_binary_data mangles a name into an identifier; matching on the
    // original names it records is more robust than guessing the mangling.
    for (int i = 0; i < PresetData::namedResourceListSize; ++i)
    {
        if (juce::String (PresetData::originalFilenames[i]) != fileName)
            continue;

        int size = 0;

        if (const auto* data = PresetData::getNamedResource (PresetData::namedResourceList[i],
                                                             size);
            data != nullptr && size > 0)
            return juce::String::fromUTF8 (data, size);
    }

    return {};
}

const std::vector<Preset>& PresetLibrary::all()
{
    static const std::vector<Preset> loaded = []
    {
        std::vector<Preset> presets;

        for (const auto& entry : PresetFactory::presets())
        {
            const auto json = jsonFor (entry.fileName);

            if (json.isEmpty())
                continue;

            // A preset that will not load is left OUT rather than substituted
            // from the factory: the point of reading the files is that what the
            // picker offers is what shipped, and a silent fallback would hide
            // exactly the drift a test is there to catch.
            if (const auto result = PresetSerializer::fromJsonString (json); result.ok())
                presets.push_back (result.preset);
        }

        return presets;
    }();

    return loaded;
}

namespace
{

std::vector<Preset> matching (const juce::String& kind, const juce::String& typeId)
{
    std::vector<Preset> out;

    for (const auto& preset : PresetLibrary::all())
        if (preset.kind == kind && preset.typeId == typeId)
            out.push_back (preset);

    return out;
}

} // namespace

std::vector<Preset> PresetLibrary::presetsFor (EffectType type)
{
    return matching ("effect", effectTypeToString (type));
}

std::vector<Preset> PresetLibrary::presetsFor (InstrumentType type)
{
    return matching ("instrument", instrumentTypeToString (type));
}

} // namespace dew
