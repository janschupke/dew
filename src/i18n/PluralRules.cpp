#include "i18n/PluralRules.h"

namespace dew
{

namespace
{

/** A juce::StringRef's bytes as a std::string_view, which is what the generated
    table reads. Both are UTF-8 and neither owns anything, so this copies no
    characters - and a language subtag is ASCII in every locale there is. */
std::string_view viewOf (juce::StringRef text) noexcept
{
    return text.text.getAddress();
}

} // namespace

PluralCategory pluralFor (juce::StringRef locale, juce::int64 count) noexcept
{
    return plural::categoryFor (viewOf (locale), (std::int64_t) count);
}

bool isPluralCategoryName (juce::StringRef name) noexcept
{
    return plural::isCategoryName (viewOf (name));
}

PluralCategory pluralCategoryFor (juce::StringRef name) noexcept
{
    return plural::categoryNamed (viewOf (name));
}

const char* nameOfPluralCategory (PluralCategory category) noexcept
{
    return plural::nameOfCategory (category);
}

} // namespace dew
