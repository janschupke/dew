#pragma once

#include <string_view>
#include <vector>

#include "lang/SourceRange.h"
#include "lang/TokenKind.h"

namespace dew::lang
{

struct Token
{
    TokenKind kind = TokenKind::endOfFile;
    SourceRange range;

    std::string_view textIn (std::string_view source) const noexcept
    {
        return range.textIn (source);
    }
};

/** Turns source into tokens, and never fails.

    There is no error path: anything unrecognised becomes a single-byte
    `unknown` token and the scan continues. Reporting is the parser's job,
    which keeps one rule about what a diagnostic is and where it comes from,
    and means a stray character costs one message rather than truncating the
    file.

    The token stream always ends with exactly one `endOfFile` whose range is
    empty and sits at the end of the source - so every parser lookahead has
    something to read and no caller needs a bounds check.
*/
std::vector<Token> tokenize (std::string_view source);

/** The same, with comments dropped. What the parser consumes; the editor wants
    the full stream so it can colour them.
*/
std::vector<Token> tokenizeWithoutTrivia (std::string_view source);

} // namespace dew::lang
