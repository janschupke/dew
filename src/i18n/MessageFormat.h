#pragma once

#include <juce_core/juce_core.h>

#include "i18n/Arg.h"

namespace dew
{

/** `message`, with its arguments substituted, in `locale`'s plural rules.

    An ICU MessageFormat SUBSET, hand-rolled:

        {name}                                      a substitution
        {count, plural, one {# bar} other {# bars}} a count, by category
        {kind, select, wav {WAV} other {audio}}     a value, by name

    '#' inside a plural branch is the count. A literal brace is written by
    quoting it - '{' - which is ICU's own escape and the reason a message can
    still say what a brace looks like.

    Not ICU itself: the library is a pinned dependency, a THIRD_PARTY row and
    tens of megabytes of CLDR data, and dew uses three constructs. The syntax is
    ICU's so that a translator's tooling understands the catalogue; the
    implementation is ours because the subset is small enough to read.

    A message that does not parse is returned as written rather than dropped -
    a visible defect beats a blank label, for the same reason a missing key
    returns its own path.
*/
juce::String formatMessage (juce::StringRef message, const Args& arguments, juce::StringRef locale);

/** Whether `message` is a shape the formatter understands, for the gate that
    holds every catalogue value to it. */
bool isWellFormedMessage (juce::StringRef message);

} // namespace dew
