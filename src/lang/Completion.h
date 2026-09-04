#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lang/Schema.h"
#include "lang/SourceRange.h"

namespace dew::lang
{

/** What kind of thing a candidate is, so the editor can say so without
    re-deriving it.
*/
enum class CompletionKind
{
    block, ///< a block keyword: song, channel, section
    key,   ///< a key inside the current block
    value, ///< a member of a closed set, a duration, a chord
    name   ///< something declared elsewhere in this file
};

struct Completion
{
    /** What gets inserted. Language SYNTAX - `harmony`, `1/8t`, `V7/vi` - and
        therefore the same in every locale. It is never translated, for the same
        reason a channel's name in a demo is never translated: it is a token the
        compiler has to read back. */
    std::string text;

    /** What this is, in the reader's language: the schema's own doc, or the
        kind's noun phrase. Resolved here rather than carried as a Msg because
        the editor renders it and the tools print it, and neither should have to
        know which locale a completion was asked for in. */
    std::string detail;
    CompletionKind kind = CompletionKind::value;
};

struct CompletionResult
{
    /** The partial word under the caret, which the editor replaces. Empty when
        the caret is not inside a word, in which case the text is inserted.
    */
    SourceRange replacing;

    /** The block the caret is in - `unknown` at the top level. Exposed because
        it is the whole answer to "what may be written here", and a test that
        can read it does not have to infer it from the candidates.
    */
    BlockKind block = BlockKind::unknown;

    std::vector<Completion> items;
};

/** What may be written at this byte offset.

    A pure function of the source, and therefore testable without an editor: a
    test says "after `key F ` the candidates are exactly the modes".

    Works on a file that does not compile, which is the only file anybody is
    ever editing - names are collected by a resolve that runs whether or not the
    parse produced errors. Inside a comment or a string it offers nothing, which
    is the difference between completion and interference.
*/
CompletionResult completionsAt (std::string_view source, std::uint32_t byteOffset,
                                Locale locale = referenceLocale);

} // namespace dew::lang
