#include "lang/Parser.h"

#include <algorithm>

#include "lang/Lexer.h"

namespace dew::lang
{

namespace
{

constexpr std::string_view topLevelKeywords[] = {
    "song", "channel", "voicing", "rhythm", "harmony", "section", "arrangement"
};

/** How deep a block may nest before the parser gives up.

    A brace-per-character file would otherwise recurse once per byte, and the
    fuzz pass writes exactly that. The limit is far above anything the language
    needs - section > part > melody is three.
*/
constexpr int maxNesting = 24;

class Parser
{
public:
    Parser (std::string_view src, DiagnosticBag& bag)
        : source (src), diagnostics (bag), tokens (tokenizeWithoutTrivia (src))
    {
        lineOfToken.reserve (tokens.size());

        for (const auto& token : tokens)
            lineOfToken.push_back (bag.lines().lineAt (token.range.begin));
    }

    Document parseDocument()
    {
        Document document;

        while (! atEnd())
        {
            const auto before = position;

            if (peek().kind == TokenKind::word && isTopLevelKeyword (textOf (peek())))
            {
                document.blocks.push_back (parseBlock (0));
            }
            else
            {
                diagnostics.error ("E101",
                                   "expected one of song, channel, voicing, rhythm, "
                                   "harmony, section or arrangement",
                                   peek().range,
                                   "not a block keyword");
                resyncToTopLevel();
            }

            // Belt and braces: every branch above must consume something, or a
            // malformed file loops forever. The fuzz pass is what finds this.
            if (position == before)
                advance();
        }

        return document;
    }

private:
    // --- token access -------------------------------------------------------
    const Token& peek (std::size_t ahead = 0) const
    {
        const auto at = std::min (position + ahead, tokens.size() - 1);
        return tokens[at];
    }

    std::string_view textOf (const Token& token) const { return token.textIn (source); }

    bool atEnd() const { return peek().kind == TokenKind::endOfFile; }

    void advance()
    {
        if (position + 1 < tokens.size())
            ++position;
    }

    int lineAt (std::size_t index) const
    {
        return lineOfToken[std::min (index, lineOfToken.size() - 1)];
    }

    bool startsNewLine() const
    {
        return position == 0 || lineAt (position) != lineAt (position - 1);
    }

    // --- recovery -----------------------------------------------------------
    void skipToEndOfLine()
    {
        const auto line = lineAt (position);

        while (! atEnd() && lineAt (position) == line)
            advance();
    }

    void resyncToTopLevel()
    {
        auto depth = 0;

        while (! atEnd())
        {
            const auto& token = peek();

            if (token.kind == TokenKind::braceOpen)
                ++depth;
            else if (token.kind == TokenKind::braceClose)
                depth = std::max (0, depth - 1);
            else if (depth == 0 && token.kind == TokenKind::word
                     && startsNewLine() && isTopLevelKeyword (textOf (token)))
                return;

            advance();
        }
    }

    /** Consumes a balanced brace group, for recovering inside a broken block. */
    void skipBalancedBody()
    {
        if (peek().kind != TokenKind::braceOpen)
            return;

        auto depth = 0;

        do
        {
            if (peek().kind == TokenKind::braceOpen)
                ++depth;
            else if (peek().kind == TokenKind::braceClose)
                --depth;

            advance();
        }
        while (! atEnd() && depth > 0);
    }

    Value valueOf (const Token& token) const
    {
        return { token.kind, token.range, textOf (token) };
    }

    // --- blocks -------------------------------------------------------------
    Block parseBlock (int depth)
    {
        Block block;
        block.keyword = textOf (peek());
        block.keywordRange = peek().range;
        block.range = peek().range;
        advance();

        const auto headerLine = lineAt (position == 0 ? 0 : position - 1);

        // The header is every token before the '{', on the same line. Stopping
        // at the line end matters: a `harmony lament` reference with no body is
        // a statement, and without the line rule it would swallow whatever
        // block came next as its header.
        while (! atEnd() && peek().kind != TokenKind::braceOpen
               && lineAt (position) == headerLine)
        {
            block.header.push_back (valueOf (peek()));
            advance();
        }

        if (peek().kind != TokenKind::braceOpen)
        {
            diagnostics.error ("E102",
                               "`" + std::string (block.keyword) + "` needs a body",
                               block.range, "expected `{` after this");

            block.range.end = block.header.empty() ? block.range.end
                                                   : block.header.back().range.end;
            return block;
        }

        const auto openRange = peek().range;
        advance();

        if (depth >= maxNesting)
        {
            diagnostics.error ("E103", "blocks are nested too deeply here", openRange);
            skipBalancedBody();
            block.range.end = peek().range.end;
            return block;
        }

        if (hasChordBody (block.keyword))
            parseChordBody (block);
        else if (hasRhythmBody (block.keyword))
            parseRhythmBody (block);
        else if (hasArrangementBody (block.keyword))
            parseArrangementBody (block, depth);
        else
            parseStatementBody (block, depth);

        if (peek().kind == TokenKind::braceClose)
        {
            block.range.end = peek().range.end;
            advance();
        }
        else
        {
            // Reported ONCE, at the brace that was never closed, with the end of
            // the file as the second witness. A cascade of "unexpected token" on
            // every following line is what this replaces.
            auto& d = diagnostics.error ("E104", "this `{` is never closed", openRange);
            d.related.push_back ({ peek().range, "the file ends" });

            block.range.end = peek().range.end;
        }

        return block;
    }

    void parseStatementBody (Block& block, int depth)
    {
        while (! atEnd() && peek().kind != TokenKind::braceClose)
        {
            const auto before = position;

            if (peek().kind != TokenKind::word)
            {
                diagnostics.error ("E105", "expected a key", peek().range,
                                   "a statement starts with a name");
                skipToEndOfLine();

                if (position == before)
                    advance();

                continue;
            }

            // A block is a keyword whose statement reaches a '{' on the same
            // line; anything else is a plain statement. Two tokens of lookahead
            // tell `rhythm pulse` (a reference) from `rhythm pulse { ... }` (a
            // declaration) without the grammar needing separate keywords.
            if (looksLikeNestedBlock())
                block.children.push_back (parseBlock (depth + 1));
            else
                block.statements.push_back (parseStatement());

            if (position == before)
                advance();
        }
    }

    bool looksLikeNestedBlock() const
    {
        const auto line = lineAt (position);

        for (auto ahead = position; ahead < tokens.size(); ++ahead)
        {
            if (lineAt (ahead) != line || tokens[ahead].kind == TokenKind::endOfFile)
                return false;

            if (tokens[ahead].kind == TokenKind::braceOpen)
                return true;

            if (tokens[ahead].kind == TokenKind::braceClose)
                return false;
        }

        return false;
    }

    Statement parseStatement()
    {
        Statement statement;
        statement.key = textOf (peek());
        statement.keyRange = peek().range;
        statement.range = peek().range;

        const auto line = lineAt (position);
        advance();

        while (! atEnd() && lineAt (position) == line
               && peek().kind != TokenKind::braceClose)
        {
            statement.values.push_back (valueOf (peek()));
            statement.range.end = peek().range.end;
            advance();
        }

        return statement;
    }

    // --- chord bodies -------------------------------------------------------
    void parseChordBody (Block& block)
    {
        while (! atEnd() && peek().kind != TokenKind::braceClose)
        {
            const auto before = position;

            // A `key F minor` statement may sit inside a harmony body; a chord
            // never starts with a lower-case keyword the schema knows.
            if (peek().kind == TokenKind::word && textOf (peek()) == "key")
            {
                block.statements.push_back (parseStatement());
                continue;
            }

            if (peek().kind == TokenKind::bar)
            {
                if (! block.chords.empty())
                    block.chords.back().barCheckAfter = true;
                else
                    diagnostics.error ("E106", "a bar check needs a chord before it",
                                       peek().range);

                advance();
                continue;
            }

            if (peek().kind != TokenKind::word)
            {
                diagnostics.error ("E107", "expected a chord", peek().range,
                                   "not a chord symbol");
                skipToEndOfLine();

                if (position == before)
                    advance();

                continue;
            }

            block.chords.push_back (parseChordEntry());

            if (position == before)
                advance();
        }
    }

    ChordEntry parseChordEntry()
    {
        ChordEntry entry;
        entry.root = textOf (peek());
        entry.rootRange = peek().range;
        entry.range = peek().range;
        advance();

        if (peek().kind == TokenKind::caret)
        {
            const auto caretRange = peek().range;
            advance();

            if (peek().kind == TokenKind::number)
            {
                entry.hasInversion = true;
                entry.inversion = readSmallInt (textOf (peek()));
                entry.range.end = peek().range.end;
                advance();
            }
            else
            {
                diagnostics.error ("E108", "`^` needs an inversion number", caretRange,
                                   "try `^1`");
            }
        }

        if (peek().kind == TokenKind::slash)
        {
            const auto slashRange = peek().range;
            advance();

            if (peek().kind == TokenKind::word)
            {
                entry.of = textOf (peek());
                entry.range.end = peek().range.end;
                advance();
            }
            else
            {
                diagnostics.error ("E109", "`/` needs a chord to tonicise", slashRange,
                                   "try `/V`");
            }
        }

        if (peek().kind == TokenKind::repeat)
        {
            entry.hasWeight = true;
            entry.weight = readSmallInt (textOf (peek()).substr (1));
            entry.range.end = peek().range.end;
            advance();
        }
        else
        {
            // An absolute length: `2 bars`, `1/4`. Collected raw and left to the
            // resolver, like every other value.
            const auto line = lineAt (position);

            while (! atEnd() && lineAt (position) == line
                   && (peek().kind == TokenKind::number || peek().kind == TokenKind::ratio
                       || (peek().kind == TokenKind::word && isDurationUnit (textOf (peek())))))
            {
                entry.duration.push_back (valueOf (peek()));
                entry.range.end = peek().range.end;
                advance();
            }
        }

        return entry;
    }

    static bool isDurationUnit (std::string_view text) noexcept
    {
        return text == "bar" || text == "bars" || text == "beat" || text == "beats";
    }

    // --- rhythm bodies ------------------------------------------------------
    void parseRhythmBody (Block& block)
    {
        while (! atEnd() && peek().kind != TokenKind::braceClose)
        {
            const auto before = position;

            RhythmEntry entry;
            entry.range = peek().range;

            if (peek().kind == TokenKind::ratio)
            {
                entry.kind = RhythmEntry::Kind::duration;
                entry.text = textOf (peek());
                advance();
            }
            else if (peek().kind == TokenKind::rest || peek().kind == TokenKind::tie)
            {
                // `-` and `~` are PREFIXES carrying their own length: `- 1/4` is
                // a quarter rest, `~ 1/2` holds the previous note a half longer.
                // A bare `-` would need a length from somewhere, and every
                // candidate - the previous entry's, a fixed default - is a rule
                // you have to remember rather than read.
                entry.kind = peek().kind == TokenKind::rest ? RhythmEntry::Kind::rest
                                                            : RhythmEntry::Kind::tie;
                const auto markRange = peek().range;
                advance();

                if (peek().kind != TokenKind::ratio)
                {
                    diagnostics.error ("E114",
                                       entry.kind == RhythmEntry::Kind::rest
                                           ? "a rest needs a length"
                                           : "a tie needs a length",
                                       markRange,
                                       entry.kind == RhythmEntry::Kind::rest
                                           ? "try `- 1/4`" : "try `~ 1/2`");
                    continue;
                }

                entry.text = textOf (peek());
                entry.range.end = peek().range.end;
                advance();
            }
            else
            {
                diagnostics.error ("E110", "expected a duration, `-` or `~`", peek().range,
                                   "not a rhythm entry");
                skipToEndOfLine();

                if (position == before)
                    advance();

                continue;
            }

            if (peek().kind == TokenKind::repeat)
            {
                entry.repeat = readSmallInt (textOf (peek()).substr (1));
                entry.range.end = peek().range.end;
                advance();
            }

            block.rhythm.push_back (entry);

            if (position == before)
                advance();
        }
    }

    // --- arrangement bodies -------------------------------------------------
    void parseArrangementBody (Block& block, int depth)
    {
        while (! atEnd() && peek().kind != TokenKind::braceClose)
        {
            const auto before = position;

            if (peek().kind != TokenKind::word)
            {
                diagnostics.error ("E111", "expected a section name", peek().range);
                skipToEndOfLine();

                if (position == before)
                    advance();

                continue;
            }

            ArrangementEntry entry;
            entry.section = textOf (peek());
            entry.sectionRange = peek().range;
            entry.range = peek().range;

            const auto line = lineAt (position);
            advance();

            while (! atEnd() && lineAt (position) == line)
            {
                if (peek().kind == TokenKind::repeat)
                {
                    entry.repeat = readSmallInt (textOf (peek()).substr (1));
                    entry.range.end = peek().range.end;
                    advance();
                }
                else if (peek().kind == TokenKind::word && textOf (peek()) == "identical")
                {
                    entry.identical = true;
                    entry.range.end = peek().range.end;
                    advance();
                }
                else if (peek().kind == TokenKind::word && textOf (peek()) == "as")
                {
                    const auto asRange = peek().range;
                    advance();

                    if (peek().kind == TokenKind::word)
                    {
                        entry.label = textOf (peek());
                        entry.range.end = peek().range.end;
                        advance();
                    }
                    else
                    {
                        diagnostics.error ("E112", "`as` needs a label", asRange,
                                           "try `as verse_b`");
                    }
                }
                else if (peek().kind == TokenKind::braceOpen)
                {
                    // The override body reuses the block parser by wrapping it
                    // in a synthetic block, so per-instance overrides and a
                    // section body cannot drift apart.
                    entry.overrides.push_back (parseOverrideBlock (depth + 1));
                    entry.range.end = entry.overrides.back().range.end;
                    break;
                }
                else
                {
                    diagnostics.error ("E113", "unexpected in an arrangement entry",
                                       peek().range);
                    skipToEndOfLine();
                    break;
                }
            }

            block.arrangement.push_back (entry);

            if (position == before)
                advance();
        }
    }

    Block parseOverrideBlock (int depth)
    {
        Block block;
        block.keyword = "overrides";
        block.keywordRange = peek().range;
        block.range = peek().range;

        const auto openRange = peek().range;
        advance();

        if (depth < maxNesting)
        {
            parseStatementBody (block, depth);
        }
        else
        {
            diagnostics.error ("E103", "blocks are nested too deeply here", openRange);
        }

        if (peek().kind == TokenKind::braceClose)
        {
            block.range.end = peek().range.end;
            advance();
        }
        else
        {
            auto& d = diagnostics.error ("E104", "this `{` is never closed", openRange);
            d.related.push_back ({ peek().range, "the file ends" });
            block.range.end = peek().range.end;
        }

        return block;
    }

    static int readSmallInt (std::string_view text) noexcept
    {
        auto value = 0;

        for (const auto c : text)
        {
            if (c < '0' || c > '9')
                break;

            // Clamped rather than wrapped: `x999999999999` is a silly number,
            // not undefined behaviour.
            if (value > 100000)
                return value;

            value = value * 10 + (c - '0');
        }

        return value;
    }

    std::string_view source;
    DiagnosticBag& diagnostics;
    std::vector<Token> tokens;
    std::vector<int> lineOfToken;
    std::size_t position = 0;
};

} // namespace

bool isTopLevelKeyword (std::string_view text) noexcept
{
    return std::find (std::begin (topLevelKeywords), std::end (topLevelKeywords), text)
         != std::end (topLevelKeywords);
}

bool hasChordBody (std::string_view keyword) noexcept { return keyword == "harmony"; }
bool hasRhythmBody (std::string_view keyword) noexcept { return keyword == "rhythm"; }
bool hasArrangementBody (std::string_view keyword) noexcept { return keyword == "arrangement"; }

Document parse (std::string_view source, DiagnosticBag& diagnostics)
{
    Parser parser { source, diagnostics };
    return parser.parseDocument();
}

} // namespace dew::lang
