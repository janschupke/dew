#include "model/DemoLibrary.h"

#include <DemoData.h>

#include "i18n/Strings.h"
#include "model/ProjectSerializer.h"

namespace dew
{

juce::String DemoLibrary::jsonFor (const juce::String& fileName)
{
    // juce_add_binary_data mangles a name into an identifier; matching on the
    // original names it records is more robust than guessing the mangling.
    for (int i = 0; i < DemoData::namedResourceListSize; ++i)
    {
        if (juce::String (DemoData::originalFilenames[i]) != fileName)
            continue;

        int size = 0;

        if (const auto* data = DemoData::getNamedResource (DemoData::namedResourceList[i], size);
            data != nullptr && size > 0)
            return juce::String::fromUTF8 (data, size);
    }

    return {};
}

juce::ValueTree DemoLibrary::load (int index, juce::StringArray& warnings)
{
    const auto& all = entries();

    if (index < 0 || index >= (int) all.size())
        return {};

    const auto json = jsonFor (all[(size_t) index].fileName);

    if (json.isEmpty())
    {
        warnings.add (
            tr (StringId::demo_missing, Args {}.with ("name", all[(size_t) index].menuName)));
        return {};
    }

    auto loaded = ProjectSerializer::fromJsonString (json);

    if (! loaded.ok())
    {
        warnings.add (loaded.result.getErrorMessage());
        return {};
    }

    warnings.addArray (loaded.warnings);
    return loaded.tree;
}

} // namespace dew
