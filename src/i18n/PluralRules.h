#pragma once

#include <juce_core/juce_core.h>

#include "i18n/PluralTable.h"

namespace dew
{

/** CLDR's plural categories, and the rule that picks one.

    The rules themselves are GENERATED, into i18n/PluralTable.h and into
    dew_lang's copy of the same file, from resources/i18n/plurals.txt. That file
    says why they are duplicated: the two libraries cannot share a header, and a
    rule transcribed twice by hand is a rule that will eventually disagree with
    itself in the one locale nobody here reads.

    What is left in this layer is the JUCE face - juce::StringRef in, const char*
    out - so nothing above has to know the table is a std::string_view affair.
*/
using PluralCategory = plural::Category;

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
