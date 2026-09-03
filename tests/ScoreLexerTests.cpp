#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "lang/Lexer.h"
#include "lang/SourceRange.h"

using namespace dew::lang;

namespace
{

constexpr const char* kSource = "// amber.score\n"
                                "song {\n"
                                "  title  \"Amber\"\n"
                                "  tempo  96 bpm\n"
                                "  meter  4/4\n"
                                "  seed   0x5EEDC0FFEE\n"
                                "}\n"
                                "\n"
                                "channel pad { colour #4C6EF5  range C3..C5  velocity 72 +- 6 }\n"
                                "\n"
                                "rhythm running { 1/8 1/8 1/4. - 1/8t ~ 1/2 }\n"
                                "\n"
                                "harmony lament {\n"
                                "  i x2 | bVI | bVII | i^1 x2 | iv | V7/iv\n"
                                "}\n";

/** The lexemes of every non-trivia, non-EOF token. */
std::vector<std::string> lexemes (std::string_view source)
{
    std::vector<std::string> out;

    for (const auto& t : tokenizeWithoutTrivia (source))
        if (t.kind != TokenKind::endOfFile)
            out.emplace_back (t.textIn (source));

    return out;
}

} // namespace

TEST_CASE ("every token's range indexes back to its own lexeme", "[score][lexer]")
{
    // The property the diagnostics overlay depends on: a range is not a label,
    // it is a substring. If this drifts, every squiggle lands somewhere else.
    const std::string_view source { kSource };

    auto checked = 0;

    for (const auto& token : tokenize (source))
    {
        REQUIRE (token.range.begin <= token.range.end);
        REQUIRE (token.range.end <= (std::uint32_t) source.size());

        if (token.kind != TokenKind::endOfFile)
        {
            REQUIRE (token.range.length() > 0);
            ++checked;
        }
    }

    // The control case: a gate that examined nothing would pass too.
    INFO ("tokens checked " << checked);
    REQUIRE (checked > 40);
}

TEST_CASE ("the stream always ends with exactly one end-of-file", "[score][lexer]")
{
    for (const auto* source : { "", "   ", "song", "song {", "\n\n\n", "\"unterminated" })
    {
        INFO ("source " << source);

        const auto tokens = tokenize (source);
        REQUIRE_FALSE (tokens.empty());
        REQUIRE (tokens.back().kind == TokenKind::endOfFile);

        auto ends = 0;

        for (const auto& t : tokens)
            if (t.kind == TokenKind::endOfFile)
                ++ends;

        REQUIRE (ends == 1);

        // Empty, and AT the end - never past it, which is what would make the
        // overlay draw off the end of the document.
        REQUIRE (tokens.back().range.isEmpty());
        REQUIRE (tokens.back().range.begin == (std::uint32_t) std::string_view (source).size());
    }
}

TEST_CASE ("an unterminated string ends at the line, not at the file", "[score][lexer]")
{
    // Single-line strings are what let the editor tokenise one line at a time
    // and stay exact. A runaway quote must therefore cost one line.
    const std::string_view source = "title \"Amber\nsong {\n}\n";

    const auto tokens = tokenizeWithoutTrivia (source);
    REQUIRE (tokens.size() > 3);

    REQUIRE (tokens[1].kind == TokenKind::text);
    REQUIRE (tokens[1].textIn (source) == "\"Amber");

    // And the rest of the file still lexes as itself.
    REQUIRE (tokens[2].kind == TokenKind::word);
    REQUIRE (tokens[2].textIn (source) == "song");
}

TEST_CASE ("line endings do not move a token", "[score][lexer]")
{
    // The same logical source in three flavours must report identical line and
    // column for every token - otherwise a file authored on another platform
    // gets diagnostics pointing at the wrong place.
    const std::string lf = "song {\n  tempo 96\n}\n";
    const std::string crlf = "song {\r\n  tempo 96\r\n}\r\n";
    const std::string tabs = "song {\n\ttempo 96\n}\n";

    auto positions = [] (const std::string& s)
    {
        const LineIndex index { s };
        std::vector<std::pair<int, int>> out;

        for (const auto& t : tokenizeWithoutTrivia (s))
            out.emplace_back (index.lineAt (t.range.begin), index.columnAt (t.range.begin));

        return out;
    };

    const auto a = positions (lf);
    const auto b = positions (crlf);

    REQUIRE (a.size() == b.size());
    REQUIRE (a == b);

    // A tab is ONE character and is not expanded to a tab stop, so one tab puts
    // `tempo` in column 2 where two spaces put it in column 3. Asserted rather
    // than assumed, because an editor that renders tabs wider is exactly where
    // a squiggle would drift if this ever changed.
    // Token 2 is `tempo` - token 1 is the opening brace.
    const auto c = positions (tabs);
    REQUIRE (a.size() == c.size());
    REQUIRE (a[2].first == c[2].first); // same line
    REQUIRE (a[2].second == 3);
    REQUIRE (c[2].second == 2);
}

TEST_CASE ("a column counts characters, not bytes", "[score][lexer]")
{
    // The compiler reports byte offsets; a HUMAN-facing column has to count
    // characters or a caret printed under a line with an em dash in it lands two
    // columns to the right of the token it is pointing at.
    const std::string source = "// an em dash \xe2\x80\x94 here\nsong {\n";
    const LineIndex index { source };

    const auto songAt = (std::uint32_t) source.find ("song");

    REQUIRE (index.lineAt (songAt) == 2);
    REQUIRE (index.columnAt (songAt) == 1);

    // "// an em dash - here" is 20 characters but 22 bytes, because the dash is
    // three bytes. So the column at the end of the line is 21, not 23: that gap
    // is precisely the drift a byte-counting column would print.
    const auto lineOne = index.lineTextAt (0);
    REQUIRE (lineOne.size() == 22);
    REQUIRE (index.columnAt ((std::uint32_t) lineOne.size()) == 21);
}

TEST_CASE ("the shapes the language actually needs each lex as one token", "[score][lexer]")
{
    struct Case
    {
        const char* source;
        TokenKind kind;
    };

    const Case cases[] = {
        { "1/4", TokenKind::ratio },  // a note value
        { "4/4", TokenKind::ratio },  // and a meter - context decides
        { "1/4.", TokenKind::ratio }, // dotted
        { "1/8t", TokenKind::ratio }, // triplet
        { "96", TokenKind::number },
        { "0.25", TokenKind::number },
        { "-1", TokenKind::number },
        { "+5", TokenKind::number },
        { "0x5EEDC0FFEE", TokenKind::number },
        { "x2", TokenKind::repeat },
        { "\"Amber\"", TokenKind::text },
        { "#4C6EF5", TokenKind::colour },
        { "bVII", TokenKind::word },
        { "F#dim7", TokenKind::word }, // '#' is a sharp inside a word
        { "verse_b", TokenKind::word },
        { "root-fifth", TokenKind::word }, // a hyphen between letters
        { "harmonic-minor", TokenKind::word },
        { "root-third-fifth", TokenKind::word },
        { "// a comment", TokenKind::comment },
        { "..", TokenKind::range },
        { "+-", TokenKind::plusMinus },
        { "|", TokenKind::bar },
        { "^", TokenKind::caret },
        { "/", TokenKind::slash },
    };

    for (const auto& c : cases)
    {
        INFO ("source " << c.source << " expected " << nameOf (c.kind));

        const auto tokens = tokenize (c.source);
        REQUIRE (tokens.size() == 2); // the token, then end of file
        REQUIRE (tokens[0].kind == c.kind);
        REQUIRE (tokens[0].textIn (c.source) == c.source);
    }
}

TEST_CASE ("a range is not a decimal point and a sharp is not a comment", "[score][lexer]")
{
    // Three collisions the grammar has to keep apart, each of which would be
    // silent rather than loud if it went wrong.

    // '..' after a digit is a range, not a malformed decimal.
    {
        const std::string_view source = "3..5";
        const auto tokens = tokenizeWithoutTrivia (source);
        REQUIRE (tokens.size() == 4);
        REQUIRE (tokens[0].kind == TokenKind::number);
        REQUIRE (tokens[0].textIn (source) == "3");
        REQUIRE (tokens[1].kind == TokenKind::range);
        REQUIRE (tokens[2].kind == TokenKind::number);
    }

    // '#' begins a colour when hex follows and a word otherwise - never a
    // comment, which is why comments are '//'.
    {
        const std::string_view source = "#iv";
        const auto tokens = tokenizeWithoutTrivia (source);
        REQUIRE (tokens[0].kind == TokenKind::word);
        REQUIRE (tokens[0].textIn (source) == "#iv");
    }

    // A hyphen between letters joins a word; anywhere else it is a rest.
    {
        const std::string_view source = "1/4 - 1/8";
        const auto tokens = tokenizeWithoutTrivia (source);
        REQUIRE (tokens.size() == 4);
        REQUIRE (tokens[0].kind == TokenKind::ratio);
        REQUIRE (tokens[1].kind == TokenKind::rest);
        REQUIRE (tokens[2].kind == TokenKind::ratio);
    }

    // A hyphen followed by a DIGIT never joins the word before it: the word
    // ends, and the hyphen goes to the number - `-1/4` is one ratio.
    {
        const std::string_view source = "chord-tones -1/4";
        const auto tokens = tokenizeWithoutTrivia (source);
        REQUIRE (tokens.size() == 3);
        REQUIRE (tokens[0].textIn (source) == "chord-tones");
        REQUIRE (tokens[1].kind == TokenKind::ratio);
        REQUIRE (tokens[1].textIn (source) == "-1/4");
    }

    // A tonicisation slash is not a ratio, because no digit follows it.
    {
        const std::string_view source = "V7/iv";
        const auto tokens = tokenizeWithoutTrivia (source);
        REQUIRE (tokens.size() == 4);
        REQUIRE (tokens[0].textIn (source) == "V7");
        REQUIRE (tokens[1].kind == TokenKind::slash);
        REQUIRE (tokens[2].textIn (source) == "iv");
    }
}

TEST_CASE ("a rhythm and a progression lex into the pieces the parser expects", "[score][lexer]")
{
    REQUIRE (
        lexemes ("{ 1/8 1/8 1/4. - 1/8t ~ 1/2 }")
        == std::vector<std::string> { "{", "1/8", "1/8", "1/4.", "-", "1/8t", "~", "1/2", "}" });

    REQUIRE (lexemes ("i x2 | bVI | i^1 | V7/iv")
             == std::vector<std::string> { "i", "x2", "|", "bVI", "|", "i", "^", "1", "|", "V7",
                                           "/", "iv" });
}

TEST_CASE ("comments are tokens for the editor and gone for the parser", "[score][lexer]")
{
    const std::string_view source = "// a note\nsong {\n}\n";

    const auto all = tokenize (source);
    const auto parsed = tokenizeWithoutTrivia (source);

    REQUIRE (all.front().kind == TokenKind::comment);
    REQUIRE (parsed.front().kind == TokenKind::word);
    REQUIRE (all.size() == parsed.size() + 1);
}

TEST_CASE ("the lexer terminates and stays in bounds on any input", "[score][lexer]")
{
    // A deterministic sweep, not a fuzzer: every prefix of the example, plus
    // every single-byte mutation of a short source. The parser's own fuzz pass
    // lands later; this one pins the property the parser will rely on, which is
    // that the token stream is always finite and always inside the source.
    const std::string source { kSource };

    for (std::size_t n = 0; n <= source.size(); ++n)
    {
        const auto prefix = source.substr (0, n);
        const auto tokens = tokenize (prefix);

        REQUIRE_FALSE (tokens.empty());
        REQUIRE (tokens.back().kind == TokenKind::endOfFile);

        for (const auto& t : tokens)
            REQUIRE (t.range.end <= (std::uint32_t) prefix.size());
    }

    const std::string seed = "song { tempo 96 meter 4/4 }";

    for (std::size_t i = 0; i < seed.size(); ++i)
    {
        for (const char replacement : { '\0', '"', '\\', '/', '#', '{', '.', 'x', '\xe2' })
        {
            auto mutated = seed;
            mutated[i] = replacement;

            const auto tokens = tokenize (mutated);

            REQUIRE_FALSE (tokens.empty());
            REQUIRE (tokens.back().kind == TokenKind::endOfFile);

            for (const auto& t : tokens)
                REQUIRE (t.range.end <= (std::uint32_t) mutated.size());
        }
    }
}
