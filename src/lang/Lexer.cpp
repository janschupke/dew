#include "lang/Lexer.h"

#include "lang/ScanCore.h"

namespace dew::lang
{

namespace
{

// clang-format off
/** ScanCore's cursor over a std::string_view. The other instantiation lives in
    the UI layer, over a juce::CodeDocument::Iterator.
*/
class StringViewCursor
{
public:
    explicit StringViewCursor (std::string_view s) noexcept : text (s) {}

    char peek() const noexcept { return pos < text.size() ? text[pos] : '\0'; }

// clang-format on
    char peekAt (int ahead) const noexcept
    {
        const auto at = pos + (std::size_t) ahead;
        return at < text.size() ? text[at] : '\0';
    }

    void skip() noexcept
    {
        if (pos < text.size())
            ++pos;
    }

// clang-format off
    bool isEOF() const noexcept { return pos >= text.size(); }

    std::size_t offset() const noexcept { return pos; }

// clang-format on
private:
    std::string_view text;
    std::size_t pos = 0;
};

} // namespace

// clang-format off
const char* nameOf (TokenKind kind) noexcept
{
    switch (kind)
    {
        case TokenKind::endOfFile:    return "end of file";
        case TokenKind::word:         return "word";
        case TokenKind::number:       return "number";
        case TokenKind::ratio:        return "ratio";
        case TokenKind::repeat:       return "repeat";
        case TokenKind::text:         return "string";
        case TokenKind::colour:       return "colour";
        case TokenKind::braceOpen:    return "'{'";
        case TokenKind::braceClose:   return "'}'";
        case TokenKind::bracketOpen:  return "'['";
        case TokenKind::bracketClose: return "']'";
        case TokenKind::bar:          return "'|'";
        case TokenKind::range:        return "'..'";
        case TokenKind::comma:        return "','";
        case TokenKind::plusMinus:    return "'+-'";
        case TokenKind::rest:         return "'-'";
        case TokenKind::tie:          return "'~'";
        case TokenKind::slash:        return "'/'";
        case TokenKind::caret:        return "'^'";
        case TokenKind::percent:      return "'%'";
        case TokenKind::comment:      return "comment";
        case TokenKind::unknown:      return "unknown";
    }

// clang-format on
    return "unknown";
}

std::vector<Token> tokenize (std::string_view source)
{
    std::vector<Token> tokens;
    StringViewCursor cursor { source };

    for (;;)
    {
        skipSpace (cursor);

        const auto begin = (std::uint32_t) cursor.offset();
        const auto kind = scanOne (cursor);
        const auto end = (std::uint32_t) cursor.offset();

        if (kind == TokenKind::endOfFile)
        {
            // Empty range AT the end, never past it: the overlay draws whatever
            // range a diagnostic carries, and one reaching past the document
            // would draw off the end of it.
            tokens.push_back ({ TokenKind::endOfFile, { begin, begin } });
            return tokens;
        }

        // scanOne consumes at least one byte unless it hit the end, which is
        // what makes this loop terminate on any input at all. Asserting it
        // costs nothing and turns a future hang into an obvious failure.
        if (end <= begin)
        {
            tokens.push_back ({ TokenKind::endOfFile, { begin, begin } });
            return tokens;
        }

        tokens.push_back ({ kind, { begin, end } });
    }
}

std::vector<Token> tokenizeWithoutTrivia (std::string_view source)
{
    auto tokens = tokenize (source);

    std::vector<Token> kept;
    kept.reserve (tokens.size());

    for (const auto& t : tokens)
        if (! isTrivia (t.kind))
            kept.push_back (t);

    return kept;
}

} // namespace dew::lang
