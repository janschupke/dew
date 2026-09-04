#pragma once

#include <string>

#include "lang/MessageCatalog.h"
#include "lang/MessageFormat.h"

namespace dew::lang
{

/** `id`'s sentence in `locale`, with `arguments` substituted.

    The front door, and the twin of dew::tr. Every sentence the score language
    says a person - a diagnostic's message, its label, its notes and helps, the
    reference documentation dew_docs emits - comes through here.

    Never empty: a row the catalogue does not hold resolves to its own dotted
    key, because a diagnostic that says nothing is worse than one that says
    `lang.parser.expectedKey.message` and can be grepped for.

    The locale is passed rather than held. dew_lang has no mutable global state
    at all, only const tables, and the library whose whole claim is determinism
    is the last one that should gain its first: two compiles of the same score
    in the same process must not be able to differ because something in between
    them changed a language. DiagnosticBag carries one for the length of a
    compile, which is exactly as long as one is meaningful.
*/
std::string msg (Msg id, Locale locale = referenceLocale);
std::string msg (Msg id, const MsgArgs& arguments, Locale locale = referenceLocale);

} // namespace dew::lang
