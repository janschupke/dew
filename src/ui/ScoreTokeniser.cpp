#include "ui/ScoreTokeniser.h"

#include <set>

#include "lang/ScanCore.h"
#include "lang/Schema.h"
#include "ui/design/Tokens.h"

namespace dew
{

namespace
{

/** Every word the schema declares, built once from the same table the resolver
    reads.

    Not a hand-written keyword list. A second list would be a second grammar,
    and it would be wrong the first time somebody added a key - which is exactly
    the drift this whole design exists to prevent.
*/
const std::set<std::string>& schemaWords()
{
    static const std::set<std::string> words = []
    {
        std::set<std::string> out;

        for (const auto& block : lang::schema())
        {
            out.emplace (lang::nameOf (block.kind));

            for (const auto& key : block.keys)
                out.emplace (key.name);
        }

        return out;
    }();

    return words;
}

} // namespace

bool ScoreTokeniser::isSchemaWord (const std::string& word)
{
    return schemaWords().count (word) > 0;
}

int ScoreTokeniser::colourFor (lang::TokenKind kind, const std::string& lexeme)
{
    switch (kind)
    {
        case lang::TokenKind::word: return isSchemaWord (lexeme) ? keyword : plain;

        case lang::TokenKind::number:
        case lang::TokenKind::ratio:
        case lang::TokenKind::repeat:
        case lang::TokenKind::colour: return literal;

        case lang::TokenKind::text: return stringText;

        case lang::TokenKind::comment: return comment;

        case lang::TokenKind::braceOpen:
        case lang::TokenKind::braceClose:
        case lang::TokenKind::bracketOpen:
        case lang::TokenKind::bracketClose:
        case lang::TokenKind::bar:
        case lang::TokenKind::range:
        case lang::TokenKind::comma:
        case lang::TokenKind::plusMinus:
        case lang::TokenKind::rest:
        case lang::TokenKind::tie:
        case lang::TokenKind::slash:
        case lang::TokenKind::caret:
        case lang::TokenKind::percent: return punctuation;

        case lang::TokenKind::unknown: return invalid;

        case lang::TokenKind::endOfFile: break;
    }

    return plain;
}

int ScoreTokeniser::readNextToken (juce::CodeDocument::Iterator& source)
{
    CodeDocumentCursor cursor { source };

    lang::skipSpace (cursor);

    if (cursor.isEOF())
        return plain;

    cursor.lexeme.clear();
    const auto kind = lang::scanOne (cursor);

    return colourFor (kind, cursor.lexeme);
}

juce::CodeEditorComponent::ColourScheme ScoreTokeniser::scheme()
{
    juce::CodeEditorComponent::ColourScheme s;

    // The order of these calls IS the meaning of Colour's values.
    //
    // Roles, not decoration: a keyword is the accent because it is the word
    // carrying the structure; a name is ordinary text because a name is the
    // thing being said; a comment is disabled text because it is not part of
    // the program; and an unrecognised byte is danger, which is the one colour
    // that should make somebody look.
    s.set ("plain", tokens::colour::textPrimary);
    s.set ("keyword", tokens::colour::accent);
    s.set ("literal", tokens::colour::playhead);
    s.set ("string", tokens::colour::success);
    s.set ("comment", tokens::colour::textDisabled);
    s.set ("punctuation", tokens::colour::textSecondary);
    s.set ("invalid", tokens::colour::danger);

    return s;
}

juce::CodeEditorComponent::ColourScheme ScoreTokeniser::getDefaultColourScheme()
{
    return scheme();
}

} // namespace dew
