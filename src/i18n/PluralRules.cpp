#include "i18n/PluralRules.h"

namespace dew
{

namespace
{

// clang-format off
struct CategoryName
{
    PluralCategory category;
    const char* name;
};

const CategoryName categoryNames[] {
    { PluralCategory::zero,  "zero"  },
    { PluralCategory::one,   "one"   },
    { PluralCategory::two,   "two"   },
    { PluralCategory::few,   "few"   },
    { PluralCategory::many,  "many"  },
    { PluralCategory::other, "other" },
};
// clang-format on

/** The language subtag of a BCP-47 tag: "fr" from "fr-CA".

    Plural rules are a property of the language and never of the region. There
    is no locale where a country changes how a number is counted, and treating
    "fr-CA" as unknown would silently give it English's rule.
*/
juce::String languageOf (juce::StringRef locale)
{
    return juce::String (locale).upToFirstOccurrenceOf ("-", false, false).toLowerCase();
}

} // namespace

PluralCategory pluralFor (juce::StringRef locale, juce::int64 count) noexcept
{
    const auto language = languageOf (locale);
    const auto n = count < 0 ? -count : count;

    // One rule per language, transcribed from CLDR. Written out rather than
    // reduced to a shared expression: these are different rules that happen to
    // agree on small numbers, and folding them together is how a locale
    // silently inherits another's.
    if (language == "cs" || language == "sk")
    {
        // cs: one -> i = 1; few -> i = 2..4; other otherwise.
        if (n == 1)
            return PluralCategory::one;

        if (n >= 2 && n <= 4)
            return PluralCategory::few;

        return PluralCategory::other;
    }

    if (language == "pl")
    {
        // pl: one -> i = 1; few -> i % 10 = 2..4 and i % 100 != 12..14.
        if (n == 1)
            return PluralCategory::one;

        const auto tens = n % 10;
        const auto hundreds = n % 100;

        if (tens >= 2 && tens <= 4 && ! (hundreds >= 12 && hundreds <= 14))
            return PluralCategory::few;

        return PluralCategory::many;
    }

    if (language == "ja" || language == "zh" || language == "ko" || language == "th")
        return PluralCategory::other; // no grammatical plural at all

    if (language == "fr")
        return n <= 1 ? PluralCategory::one : PluralCategory::other; // 0 is singular

    // en, de, nl, sv, es, it, pt, fi and the rest of the two-form majority.
    return n == 1 ? PluralCategory::one : PluralCategory::other;
}

bool isPluralCategoryName (juce::StringRef name) noexcept
{
    for (const auto& row : categoryNames)
        if (name == juce::StringRef (row.name))
            return true;

    return false;
}

PluralCategory pluralCategoryFor (juce::StringRef name) noexcept
{
    for (const auto& row : categoryNames)
        if (name == juce::StringRef (row.name))
            return row.category;

    return PluralCategory::other;
}

const char* nameOfPluralCategory (PluralCategory category) noexcept
{
    for (const auto& row : categoryNames)
        if (row.category == category)
            return row.name;

    return "other";
}

} // namespace dew
