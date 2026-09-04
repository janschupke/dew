// =============================================================================
// The cursor, the recovery, and the generic block walk.
//
// One of two translation units defining lang/ParserImpl.h. Everything true of
// every block: peeking and advancing, skipping to a resync point after an
// error, and reading a block of key-value statements.
//
// parseBlock's four-way dispatch is the seam - three keywords take a list body
// and everything else takes statements.
// =============================================================================

#include "lang/Parser.h"

#include "lang/ParserImpl.h"

#include <algorithm>

#include "lang/ScanCore.h"

namespace dew::lang
{

Document Parser::parseDocument()
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
            diagnostics.error ("E101", diagnostics.text (Msg::parser_expectedBlock_message),
                               peek().range, diagnostics.text (Msg::parser_expectedBlock_label));
            resyncToTopLevel();
        }

        // Belt and braces: every branch above must consume something, or a
        // malformed file loops forever. The fuzz pass is what finds this.
        if (position == before)
            advance();
    }

    return document;
}

const Token& Parser::peek (std::size_t ahead) const
{
    const auto at = std::min (position + ahead, tokens.size() - 1);
    return tokens[at];
}

std::string_view Parser::textOf (const Token& token) const
{
    return token.textIn (source);
}

bool Parser::atEnd() const
{
    return peek().kind == TokenKind::endOfFile;
}

void Parser::advance()
{
    if (position + 1 < tokens.size())
        ++position;
}

int Parser::lineAt (std::size_t index) const
{
    return lineOfToken[std::min (index, lineOfToken.size() - 1)];
}

bool Parser::startsNewLine() const
{
    return position == 0 || lineAt (position) != lineAt (position - 1);
}

void Parser::skipToEndOfLine()
{
    const auto line = lineAt (position);

    while (! atEnd() && lineAt (position) == line)
        advance();
}

void Parser::resyncToTopLevel()
{
    auto depth = 0;

    while (! atEnd())
    {
        const auto& token = peek();

        if (token.kind == TokenKind::braceOpen)
            ++depth;
        else if (token.kind == TokenKind::braceClose)
            depth = std::max (0, depth - 1);
        else if (depth == 0 && token.kind == TokenKind::word && startsNewLine()
                 && isTopLevelKeyword (textOf (token)))
            return;

        advance();
    }
}

void Parser::skipBalancedBody()
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
    } while (! atEnd() && depth > 0);
}

Value Parser::valueOf (const Token& token) const
{
    return { token.kind, token.range, textOf (token) };
}

Block Parser::parseBlock (int depth)
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
    while (! atEnd() && peek().kind != TokenKind::braceOpen && lineAt (position) == headerLine)
    {
        block.header.push_back (valueOf (peek()));
        advance();
    }

    if (peek().kind != TokenKind::braceOpen)
    {
        diagnostics.error ("E102",
                           diagnostics.text (Msg::parser_needsBody_message,
                                             MsgArgs {}.with ("keyword", block.keyword)),
                           block.range, diagnostics.text (Msg::parser_needsBody_label));

        block.range.end = block.header.empty() ? block.range.end : block.header.back().range.end;
        return block;
    }

    const auto openRange = peek().range;
    advance();

    if (depth >= maxNesting)
    {
        diagnostics.error ("E103", diagnostics.text (Msg::parser_tooDeep_message), openRange);
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
        auto& d = diagnostics.error (
            "E104", diagnostics.text (Msg::parser_braceNeverClosed_message), openRange);
        d.related.push_back (
            { peek().range, diagnostics.text (Msg::parser_braceNeverClosed_related) });

        block.range.end = peek().range.end;
    }

    return block;
}

void Parser::parseStatementBody (Block& block, int depth)
{
    while (! atEnd() && peek().kind != TokenKind::braceClose)
    {
        const auto before = position;

        if (peek().kind != TokenKind::word)
        {
            diagnostics.error ("E105", diagnostics.text (Msg::parser_expectedKey_message),
                               peek().range, diagnostics.text (Msg::parser_expectedKey_label));
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

bool Parser::looksLikeNestedBlock() const
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

Statement Parser::parseStatement()
{
    Statement statement;
    statement.key = textOf (peek());
    statement.keyRange = peek().range;
    statement.range = peek().range;

    const auto line = lineAt (position);
    advance();

    while (! atEnd() && lineAt (position) == line && peek().kind != TokenKind::braceClose)
    {
        statement.values.push_back (valueOf (peek()));
        statement.range.end = peek().range.end;
        advance();
    }

    return statement;
}

bool isTopLevelKeyword (std::string_view text) noexcept
{
    return std::find (std::begin (topLevelKeywords), std::end (topLevelKeywords), text)
           != std::end (topLevelKeywords);
}

bool hasChordBody (std::string_view keyword) noexcept
{
    return keyword == "harmony";
}
bool hasRhythmBody (std::string_view keyword) noexcept
{
    return keyword == "rhythm";
}
bool hasArrangementBody (std::string_view keyword) noexcept
{
    return keyword == "arrangement";
}

Document parse (std::string_view source, DiagnosticBag& diagnostics)
{
    Parser parser { source, diagnostics };
    return parser.parseDocument();
}

} // namespace dew::lang
