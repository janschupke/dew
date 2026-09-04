#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** CLDR's plural categories.

    Six, because that is how many the widest locale needs, not because English
    needs them. English selects between two and Czech between four, and a
    message that names 'one' and 'other' is answered correctly in both - the
    rule picks a category, and a branch the message does not carry falls back to
    'other'.
*/
enum class PluralCategory
{
    zero,
    one,
    two,
    few,
    many,
    other
};

/** The category `count` takes in `locale`.

    The locale's language subtag decides it: fr-CA pluralises as fr does. An
    unknown language gets English's rule, which is the safest wrong answer -
    every message declares 'other', so the result is a real sentence rather
    than a missing branch.
*/
PluralCategory pluralFor (juce::StringRef locale, juce::int64 count) noexcept;

/** The category a plural branch names, for the parser. */
bool isPluralCategoryName (juce::StringRef name) noexcept;
PluralCategory pluralCategoryFor (juce::StringRef name) noexcept;
const char* nameOfPluralCategory (PluralCategory category) noexcept;

} // namespace dew
