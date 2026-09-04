#pragma once

#include <string_view>
#include <vector>

#include "lang/Ast.h"
#include "lang/Diagnostics.h"
#include "lang/Lexer.h"

namespace dew::lang
{

constexpr std::string_view topLevelKeywords[] = { "song",    "channel", "voicing",    "rhythm",
                                                  "harmony", "section", "arrangement" };

/** How deep a block may nest before the parser gives up.

    A brace-per-character file would otherwise recurse once per byte, and the
    fuzz pass writes exactly that. The limit is far above anything the language
    needs - section > part > melody is three.
*/
constexpr int maxNesting = 24;

/** The parser, declared so that it can be defined in more than one file.

    INTERNAL. lang/Parser.h is this layer's public surface; nothing outside the
    two Parser*.cpp files includes this. ResolverImpl.h gives the reason at
    length - a file-local class cannot be defined in two translation units.

    The split is the three LIST BODIES from the cursor and the generic block
    walk. A chord list, a rhythm line and an arrangement are the three places
    where the language stops being key-value statements and becomes a sequence,
    so each is parsed by hand; parseBlock dispatches to whichever the keyword
    calls for and everything else is shared.
*/
class Parser
{
public:
    Parser (std::string_view src, DiagnosticBag& bag)
        : source (src)
        , diagnostics (bag)
        , tokens (tokenizeWithoutTrivia (src))
    {
        lineOfToken.reserve (tokens.size());

        for (const auto& token : tokens)
            lineOfToken.push_back (bag.lines().lineAt (token.range.begin));
    }

    Document parseDocument();

private:
    // --- token access -------------------------------------------------------
    const Token& peek (std::size_t ahead = 0) const;

    std::string_view textOf (const Token& token) const;

    bool atEnd() const;

    void advance();

    int lineAt (std::size_t index) const;

    bool startsNewLine() const;

    // --- recovery -----------------------------------------------------------
    void skipToEndOfLine();

    void resyncToTopLevel();

    /** Consumes a balanced brace group, for recovering inside a broken block. */
    void skipBalancedBody();

    Value valueOf (const Token& token) const;

    // --- blocks -------------------------------------------------------------
    Block parseBlock (int depth);

    void parseStatementBody (Block& block, int depth);

    bool looksLikeNestedBlock() const;

    Statement parseStatement();

    // --- chord bodies -------------------------------------------------------
    void parseChordBody (Block& block);

    ChordEntry parseChordEntry();

    static bool isDurationUnit (std::string_view text) noexcept;

    // --- rhythm bodies ------------------------------------------------------
    void parseRhythmBody (Block& block);

    // --- arrangement bodies -------------------------------------------------
    void parseArrangementBody (Block& block, int depth);

    Block parseOverrideBlock (int depth);

    static int readSmallInt (std::string_view text) noexcept;

    std::string_view source;
    DiagnosticBag& diagnostics;
    std::vector<Token> tokens;
    std::vector<int> lineOfToken;
    std::size_t position = 0;
};

} // namespace dew::lang
