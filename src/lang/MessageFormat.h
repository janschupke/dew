#pragma once

#include <string>
#include <string_view>

#include "lang/MessageArg.h"
#include "lang/PluralTable.h"

namespace dew::lang
{

/** `message`, with its arguments substituted, under `locale`'s plural rules.

    The same ICU MessageFormat SUBSET dew_i18n's formatMessage implements:

        {name}                                      a substitution
        {count, plural, one {# bar} other {# bars}} a count, by category
        {kind, select, wav {WAV} other {audio}}     a value, by name

    '#' inside a plural branch is the count. A literal brace is written by
    quoting it - '{' - which is ICU's own escape.

    Written twice, once here over std::string and once in i18n/MessageFormat.cpp
    over juce::String, and that is deliberate. The alternative is a layering edge
    from dew_lang to dew_i18n, which puts juce_core on the link line of
    tools/dew_docs - the tool whose whole job is to demonstrate that the score
    language needs nothing. The RULES the two share are generated into both from
    resources/i18n/plurals.txt, so what is duplicated is the parsing loop and
    never the table; tests/MessageFormatCases.h holds the cases both are checked
    against, so a divergence in the loop fails in one suite and not the other.

    A message that does not parse is returned as written rather than dropped - a
    visible defect beats a blank diagnostic, for the same reason a missing key
    returns its own path.
*/
std::string formatMessage (std::string_view message, const MsgArgs& arguments,
                           std::string_view locale);

/** Whether `message` is a shape the formatter understands, for the gate that
    holds every catalogue value to it. */
bool isWellFormedMessage (std::string_view message);

} // namespace dew::lang
