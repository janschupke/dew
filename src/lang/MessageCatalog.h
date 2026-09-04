#pragma once

#include <string_view>

#include "lang/MessageIds.h"
#include "lang/PluralTable.h"

namespace dew::lang
{

/** The score language's own string catalogue, generated from the `lang.` subtree
    of resources/i18n/en.json.

    A second catalogue rather than a call into dew_i18n's, because dew_lang links
    NOTHING - JUCE included - and a gate checks that at the source. tr() returns
    a juce::String, so reaching it from here is not a layering preference that
    could be argued either way: it is a compile error, and it would be a link
    error after that.

    The isolation is what tools/dew_docs exists to prove. It links dew_lang alone
    and emits the website's language reference, so the day dew_lang can only be
    linked alongside juce_core is the day that tool stops being evidence of
    anything. See tools/CMakeLists.txt, which records the mistake twice.

    Parallel arrays indexed by Msg rather than a map, for the reason
    i18n/Catalog.h gives: the index IS the key, so a message the code names and
    the catalogue does not hold is a compile error rather than a runtime miss.
*/

/** `id`'s text in `locale`, falling back to the reference locale and then to
    the dotted key itself.

    Never empty for a valid id. A diagnostic with no message reads as a compiler
    that found something and would not say what, which is worse than one that
    prints `lang.parser.expectedKey.message` and can be grepped for.
*/
std::string_view messageText (Msg id, Locale locale = referenceLocale) noexcept;

/** `id`'s dotted key - "lang.generator.tooManyChannels.message". */
std::string_view messageKey (Msg id) noexcept;

/** The argument names `id` asks for, space-separated, "" for none.

    Recorded at generation time so a test can answer every message without
    knowing what any of them say - which is what makes "no brace reaches a
    diagnostic" checkable rather than something somebody watches for.
*/
std::string_view messageArgumentNames (Msg id) noexcept;

/** `locale`'s BCP-47 tag, for the plural rules. */
std::string_view localeTag (Locale locale) noexcept;

/** The locale a BCP-47 tag selects, by exact tag and then by language subtag,
    falling back to the reference. The application's own tag comes in here. */
Locale localeFor (std::string_view tag) noexcept;

} // namespace dew::lang
