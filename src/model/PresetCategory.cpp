#include "model/PresetCategory.h"

namespace dew
{

const std::vector<PresetCategoryDescriptor>& presetCategories()
{
    // Ordered as a picker groups them: the effect vocabulary runs quietest
    // first, so a menu reads as a scale rather than as a set. The instrument
    // categories follow, and no menu ever mixes the two.
    static const std::vector<PresetCategoryDescriptor> all {
        { PresetCategory::gentle, "gentle", StringId::preset_category_gentle },
        { PresetCategory::character, "character", StringId::preset_category_character },
        { PresetCategory::extreme, "extreme", StringId::preset_category_extreme },
        { PresetCategory::bass, "bass", StringId::preset_category_bass },
        { PresetCategory::keys, "keys", StringId::preset_category_keys },
        { PresetCategory::pads, "pads", StringId::preset_category_pads },
        { PresetCategory::texture, "texture", StringId::preset_category_texture }
    };

    return all;
}

const PresetCategoryDescriptor& presetCategoryDescriptor (PresetCategory category) noexcept
{
    const auto& all = presetCategories();
    const auto index = (size_t) category;

    jassert (index < all.size());
    return all[juce::jmin (index, all.size() - 1)];
}

std::optional<PresetCategory> presetCategoryFor (juce::StringRef id)
{
    for (const auto& descriptor : presetCategories())
        if (juce::String (descriptor.id) == id)
            return descriptor.category;

    return {};
}

juce::String presetCategoryToString (PresetCategory category)
{
    return presetCategoryDescriptor (category).id;
}

juce::String presetCategoryDisplayName (PresetCategory category)
{
    return tr (presetCategoryDescriptor (category).displayName);
}

} // namespace dew
