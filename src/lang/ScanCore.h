#pragma once

#include "lang/TokenKind.h"

namespace dew::lang
{

/** The scanner, once, over any cursor.

    There are two consumers with incompatible input types: the compiler's Lexer
    walks a std::string_view, and the editor's CodeTokeniser is handed a
    juce::CodeDocument::Iterator it must consume in place. Writing the classifier
    twice would mean the highlighting and the parser could drift, and a
    highlighter that disagrees with the parser is worse than none.

    So the classifier is written once against a minimal cursor concept and
    instantiated over both. No virtuals, no allocation, no JUCE.

    A Cursor must provide:
        char peek() const        the next character, or 0 at the end
        char peekAt (int) const  the character n ahead, or 0
        void skip()              advance one character
        bool isEOF() const

    Every scanOne call consumes AT LEAST one character unless it is already at
    the end, which is what stops a malformed file spinning the parser forever.
*/

constexpr bool isDigit (char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool isLetter (char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool isHexDigit (char c) noexcept
{
    return isDigit (c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/** A byte that can appear inside a bare word.

    Includes '#' so `F#dim7` is one token, and '_' so a name can be
    `verse_b`. Excludes '-' so `1/4 -` reads as a duration and a rest rather
    than one hyphenated word, and excludes '^' and '/' so an inversion or a
    tonicisation is its own token the parser can point a diagnostic at.
*/
constexpr bool isWordByte (char c) noexcept
{
    return isLetter (c) || isDigit (c) || c == '_' || c == '#';
}

constexpr bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/** Skips whitespace. Comments are NOT skipped - they are a token, because the
    editor colours them from this same scan.
*/
template <typename Cursor>
void skipSpace (Cursor& c)
{
    while (! c.isEOF() && isSpace (c.peek()))
        c.skip();
}

/** Consumes one token and says what it was. Assumes leading whitespace is gone. */
template <typename Cursor>
TokenKind scanOne (Cursor& c)
{
    if (c.isEOF())
        return TokenKind::endOfFile;

    const auto first = c.peek();

    // --- comment ------------------------------------------------------------
    // '//' rather than '#', because '#' is a sharp in a chord (`#iv`) and the
    // prefix of a colour (`#4C6EF5`). One of the three had to give, and comments
    // are the one with a conventional alternative.
    if (first == '/' && c.peekAt (1) == '/')
    {
        while (! c.isEOF() && c.peek() != '\n')
            c.skip();

        return TokenKind::comment;
    }

    // --- string -------------------------------------------------------------
    if (first == '"')
    {
        c.skip();

        // Single-line by design: a run-away quote costs one line, not the rest
        // of the file, and the editor's per-line tokenising stays exact.
        while (! c.isEOF() && c.peek() != '"' && c.peek() != '\n')
        {
            if (c.peek() == '\\' && c.peekAt (1) != 0 && c.peekAt (1) != '\n')
                c.skip();

            c.skip();
        }

        if (! c.isEOF() && c.peek() == '"')
            c.skip();

        return TokenKind::text;
    }

    // --- colour -------------------------------------------------------------
    if (first == '#' && isHexDigit (c.peekAt (1)))
    {
        c.skip();

        while (! c.isEOF() && isHexDigit (c.peek()))
            c.skip();

        return TokenKind::colour;
    }

    // --- repeat -------------------------------------------------------------
    // `x2`, `x16`. Recognised here rather than as a word so the parser has a
    // token to point at; the cost is that `x` followed only by digits is
    // reserved and cannot name a section.
    if ((first == 'x' || first == 'X') && isDigit (c.peekAt (1)))
    {
        c.skip();

        while (! c.isEOF() && isDigit (c.peek()))
            c.skip();

        return TokenKind::repeat;
    }

    // --- word ---------------------------------------------------------------
    if (isLetter (first) || first == '_' || first == '#')
    {
        while (! c.isEOF() && isWordByte (c.peek()))
            c.skip();

        return TokenKind::word;
    }

    // --- number or ratio ----------------------------------------------------
    if (isDigit (first) || ((first == '+' || first == '-') && isDigit (c.peekAt (1))))
    {
        if (first == '+' || first == '-')
            c.skip();

        // Hex, for a seed.
        if (c.peek() == '0' && (c.peekAt (1) == 'x' || c.peekAt (1) == 'X'))
        {
            c.skip();
            c.skip();

            while (! c.isEOF() && isHexDigit (c.peek()))
                c.skip();

            return TokenKind::number;
        }

        while (! c.isEOF() && isDigit (c.peek()))
            c.skip();

        // A '/' followed by a digit makes this a ratio - a meter or a note
        // value. Which one is context, and context is the parser's job.
        if (c.peek() == '/' && isDigit (c.peekAt (1)))
        {
            c.skip();

            while (! c.isEOF() && isDigit (c.peek()))
                c.skip();

            // Dotted and triplet modifiers are part of the note value: 1/4. and
            // 1/8t. Order is fixed so there is one spelling of each duration.
            if (c.peek() == '.' && c.peekAt (1) != '.')
                c.skip();

            if (c.peek() == 't')
                c.skip();

            return TokenKind::ratio;
        }

        // A decimal point, but never the '..' of a range.
        if (c.peek() == '.' && isDigit (c.peekAt (1)))
        {
            c.skip();

            while (! c.isEOF() && isDigit (c.peek()))
                c.skip();
        }

        return TokenKind::number;
    }

    // --- punctuation --------------------------------------------------------
    if (first == '.' && c.peekAt (1) == '.')
    {
        c.skip();
        c.skip();
        return TokenKind::range;
    }

    if ((first == '+' && c.peekAt (1) == '-') || (first == '-' && c.peekAt (1) == '+'))
    {
        c.skip();
        c.skip();
        return TokenKind::plusMinus;
    }

    c.skip();

    switch (first)
    {
        case '{': return TokenKind::braceOpen;
        case '}': return TokenKind::braceClose;
        case '[': return TokenKind::bracketOpen;
        case ']': return TokenKind::bracketClose;
        case '|': return TokenKind::bar;
        case ',': return TokenKind::comma;
        case '-': return TokenKind::rest;
        case '~': return TokenKind::tie;
        case '/': return TokenKind::slash;
        case '^': return TokenKind::caret;
        case '%': return TokenKind::percent;
        default:  break;
    }

    return TokenKind::unknown;
}

} // namespace dew::lang
