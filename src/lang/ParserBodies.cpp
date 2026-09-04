// =============================================================================
// The three list bodies.
//
// One of two translation units defining lang/ParserImpl.h.
//
// A chord list, a rhythm line and an arrangement are the three places where the
// language stops being key-value statements and becomes a SEQUENCE, so each is
// parsed by hand rather than by the generic walk. They are half the parser and
// share nothing with each other beyond the cursor.
// =============================================================================

#include "lang/Parser.h"

#include "lang/ParserImpl.h"

#include <algorithm>

#include "lang/ScanCore.h"

namespace dew::lang
{

void Parser::parseChordBody (Block& block)
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
                diagnostics.error ("E106",
                                   diagnostics.text (Msg::parser_barCheckNeedsChord_message),
                                   peek().range);

            advance();
            continue;
        }

        if (peek().kind != TokenKind::word)
        {
            diagnostics.error ("E107", diagnostics.text (Msg::parser_expectedChord_message),
                               peek().range, diagnostics.text (Msg::parser_expectedChord_label));
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

ChordEntry Parser::parseChordEntry()
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
            diagnostics.error ("E108", diagnostics.text (Msg::parser_inversionNeedsNumber_message),
                               caretRange,
                               diagnostics.text (Msg::parser_inversionNeedsNumber_label));
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
            diagnostics.error (
                "E109", diagnostics.text (Msg::parser_tonicisationNeedsChord_message), slashRange,
                diagnostics.text (Msg::parser_tonicisationNeedsChord_label));
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

bool Parser::isDurationUnit (std::string_view text) noexcept
{
    return text == "bar" || text == "bars" || text == "beat" || text == "beats";
}

void Parser::parseRhythmBody (Block& block)
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
                // Two whole messages rather than one with a noun spliced in.
                // A language whose word order differs cannot recover a sentence
                // from a frame and a noun, and both of these are short.
                const auto isRest = entry.kind == RhythmEntry::Kind::rest;

                diagnostics.error ("E114",
                                   diagnostics.text (isRest ? Msg::parser_restNeedsLength_message
                                                            : Msg::parser_tieNeedsLength_message),
                                   markRange,
                                   diagnostics.text (isRest ? Msg::parser_restNeedsLength_label
                                                            : Msg::parser_tieNeedsLength_label));
                continue;
            }

            entry.text = textOf (peek());
            entry.range.end = peek().range.end;
            advance();
        }
        else
        {
            diagnostics.error ("E110", diagnostics.text (Msg::parser_expectedRhythmEntry_message),
                               peek().range,
                               diagnostics.text (Msg::parser_expectedRhythmEntry_label));
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

void Parser::parseArrangementBody (Block& block, int depth)
{
    while (! atEnd() && peek().kind != TokenKind::braceClose)
    {
        const auto before = position;

        if (peek().kind != TokenKind::word)
        {
            diagnostics.error ("E111", diagnostics.text (Msg::parser_expectedSectionName_message),
                               peek().range);
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

        // Stops at the closing brace as well as at the line end, so
        // `arrangement { verse }` reads as one entry rather than as an
        // entry followed by something unexpected.
        while (! atEnd() && lineAt (position) == line && peek().kind != TokenKind::braceClose)
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
                    diagnostics.error ("E112", diagnostics.text (Msg::parser_labelNeeded_message),
                                       asRange, diagnostics.text (Msg::parser_labelNeeded_label));
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
                diagnostics.error ("E113",
                                   diagnostics.text (Msg::parser_unexpectedInArrangement_message),
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

Block Parser::parseOverrideBlock (int depth)
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
        diagnostics.error ("E103", diagnostics.text (Msg::parser_tooDeep_message), openRange);
    }

    if (peek().kind == TokenKind::braceClose)
    {
        block.range.end = peek().range.end;
        advance();
    }
    else
    {
        auto& d = diagnostics.error (
            "E104", diagnostics.text (Msg::parser_braceNeverClosed_message), openRange);
        d.related.push_back (
            { peek().range, diagnostics.text (Msg::parser_braceNeverClosed_related) });
        block.range.end = peek().range.end;
    }

    return block;
}

int Parser::readSmallInt (std::string_view text) noexcept
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

} // namespace dew::lang
