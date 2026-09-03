#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <string>

#include "lang/TokenKind.h"

namespace dew
{

/** A cursor over a juce::CodeDocument::Iterator, so the compiler's own scanner
    runs unchanged inside the editor.

    The whole point of the shape: `lang::scanOne` is a template over a minimal
    cursor concept, and this is its second instantiation. The editor therefore
    cannot classify a token differently from the parser, because there is no
    second copy of the classifier to drift - a highlighter that disagrees with
    the compiler is worse than none at all.

    It also records the bytes it consumed, so a caller that needs the lexeme -
    to ask whether a word is a keyword - does not have to scan the text twice.
*/
struct CodeDocumentCursor
{
    explicit CodeDocumentCursor (juce::CodeDocument::Iterator& iterator) : source (iterator) {}

    /** Anything outside ASCII is reported as one fixed byte that no rule
        matches, so it classifies as `unknown` and is consumed one character at
        a time. The compiler, walking UTF-8 bytes, would call the same character
        several unknowns instead of one - a difference that exists only for
        source that is already a lexical error, since every construct in the
        language is ASCII and anything non-ASCII inside a comment or a string is
        consumed whole by both.
    */
    static constexpr char nonAscii = '\x7f';

    char at (int offset) const
    {
        auto ahead = source;

        for (auto i = 0; i < offset; ++i)
        {
            if (ahead.isEOF())
                return 0;

            ahead.skip();
        }

        if (ahead.isEOF())
            return 0;

        const auto c = ahead.peekNextChar();

        // Zero means there is nothing there, and it has to stay zero: folding
        // it in with "not ASCII" made the end of the document look like a
        // character, which is one `unknown` token past the end of every file.
        if (c == 0)
            return 0;

        return c > 0 && c < 128 ? (char) c : nonAscii;
    }

    char peek() const { return at (0); }
    char peekAt (int offset) const { return at (offset); }

    /** Out of characters, which is not the same question as
        `juce::CodeDocument::Iterator::isEOF()`.

        After the document's final newline the iterator sits on a phantom empty
        last line: it reports that it is NOT at the end and then hands back
        nothing. Trusting it there made the editor scan one token the compiler
        never saw - an `unknown` past the end of every file. A NUL cannot appear
        in source text, so "nothing left to read" is the honest test.
    */
    bool isEOF() const { return source.isEOF() || at (0) == 0; }

    void skip()
    {
        const auto c = source.peekNextChar();

        if (c > 0 && c < 128)
            lexeme += (char) c;
        else
            lexeme += nonAscii;

        source.skip();
    }

    juce::CodeDocument::Iterator& source;
    std::string lexeme;
};

/** Colours the score language, from the compiler's scanner.

    A CodeTokeniser's return value INDEXES the colour scheme's array, so the
    order these are declared in is the order `scheme()` adds them - an
    off-by-one here paints every token in its neighbour's colour and looks
    exactly like a broken editor.
*/
class ScoreTokeniser : public juce::CodeTokeniser
{
public:
    enum Colour
    {
        plain = 0,    ///< a name: a section, a channel, a chord
        keyword,      ///< a word the schema declares - a block or a key
        literal,      ///< a number, a duration, a repeat, a colour
        stringText,   ///< "Amber"
        comment,
        punctuation,  ///< braces, bars, ranges
        invalid       ///< a byte the language has no meaning for
    };

    int readNextToken (juce::CodeDocument::Iterator&) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    /** The scheme, as a value, so a test can name a colour and look for it in a
        painted image rather than asserting on ink in general.
    */
    static juce::CodeEditorComponent::ColourScheme scheme();

    /** Which colour a token takes. The lexeme is needed only to tell a word the
        schema declares from one the user chose; that lookup reads the SAME
        declared table the resolver reads, so a key cannot exist without being
        highlighted.
    */
    static int colourFor (lang::TokenKind, const std::string& lexeme);

    /** True if this word is a block keyword or a key the schema declares. */
    static bool isSchemaWord (const std::string& word);
};

} // namespace dew
