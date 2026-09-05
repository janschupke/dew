#pragma once

#include <optional>
#include <vector>

#include "i18n/Strings.h"

namespace dew
{

/** What a preset is FOR, as opposed to what it is built from.

    A preset already had two axes - `kind` and `typeId` - and both answer the
    same question: which descriptor reads this state. Neither says anything a
    person choosing a sound would use. With three presets to a type that did not
    matter; a picker that grows past a screenful with no grouping is a list you
    scroll rather than one you read.

    Deliberately ONE vocabulary across effects and instruments rather than a
    category per type. Only presets of the same type are ever offered together -
    a reverb card offers only reverbs - so a shared enum costs nothing at the
    point of display, and it is what stops "gentle" being spelled three ways by
    the third person who adds a type.
*/
enum class PresetCategory
{
    gentle,
    character,
    extreme,
    bass,
    keys,
    pads,
    texture
};

/** One category, the way EffectDescriptor is one effect. */
struct PresetCategoryDescriptor
{
    PresetCategory category;

    /** "pads" - what a .dewpreset stores, and never what a person reads. A
        stored id that changed with the language would make a preset file mean
        different things in different builds. */
    const char* id;

    StringId displayName;
};

/** Every category, in the order a picker groups them. */
const std::vector<PresetCategoryDescriptor>& presetCategories();

const PresetCategoryDescriptor& presetCategoryDescriptor (PresetCategory) noexcept;

/** The category `id` names, or nothing.

    An optional rather than a fallback, for the reason instrumentTypeFor is one:
    a category written by a later dew must be visibly unknown, not silently
    become the first one in the table.
*/
std::optional<PresetCategory> presetCategoryFor (juce::StringRef id);

juce::String presetCategoryToString (PresetCategory);
juce::String presetCategoryDisplayName (PresetCategory);

} // namespace dew
