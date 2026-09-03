#pragma once

namespace dew::lang
{

/** What one token is, lexically.

    Deliberately SMALLER than the language's vocabulary. A `word` is any bare
    run of letters and digits: `song`, `verse`, `drop2`, `i`, `bVII`, `Cm7` are
    all one kind here, and which of them is a keyword, a declared name or a chord
    is a question the schema answers, not the lexer.

    That split is the reason the editor's highlighting can never disagree with
    the parser: both call the same classifier on the same token, so there is no
    second, approximate copy of the grammar living in the tokeniser. It also
    means a chord is not lexically distinguishable from a name - `iv` could be a
    section called "iv" - which is a real limitation and the reason chords take
    the identifier colour in v1 rather than one of their own.
*/
enum class TokenKind
{
    endOfFile,

// clang-format off
    word,           ///< song, verse, drop2, i, bVII, Cm7, F#dim7
    number,         ///< 96, 0.25, -1, +5, 0x5EEDC0FFEE
    ratio,          ///< 4/4, 1/8, 1/4., 1/8t - a meter OR a duration, by context
    repeat,         ///< x2, x16 - multiplicity
    text,           ///< "Amber"
    colour,         ///< #4C6EF5

    braceOpen,      ///< {
    braceClose,     ///< }
    bracketOpen,    ///< [
    bracketClose,   ///< ]
    bar,            ///< |  - a bar-line assertion, not a separator
    range,          ///< .. - C3..C5
    comma,          ///< ,
    plusMinus,      ///< +- - deterministic jitter
    rest,           ///< -  - a rest in a rhythm
    tie,            ///< ~  - tie to the previous
    slash,          ///< /  - tonicisation, when not part of a ratio
    caret,          ///< ^  - inversion
    percent,        ///< %

    comment,        ///< // to end of line - emitted, not skipped, so the
                    ///< editor can colour it from the same scan

    unknown         ///< anything else; always one byte, so a stray character
                    ///< costs one diagnostic rather than swallowing the file
};

// clang-format on
/** True for tokens the parser skips outright. */
constexpr bool isTrivia (TokenKind kind) noexcept
{
    return kind == TokenKind::comment;
}

/** A short, stable name, for diagnostics and for test failure output. */
const char* nameOf (TokenKind) noexcept;

} // namespace dew::lang
