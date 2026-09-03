#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

#include "lang/Parser.h"

using namespace dew::lang;

namespace
{

constexpr const char* kSource = "// amber.score\n"
                                "song {\n"
                                "  title  \"Amber\"\n"
                                "  tempo  96 bpm\n"
                                "  meter  4/4\n"
                                "  key    F minor\n"
                                "  seed   0x5EEDC0FFEE\n"
                                "}\n"
                                "\n"
                                "channel pad {\n"
                                "  instrument synth\n"
                                "  mixer      1\n"
                                "  range      C3..C5\n"
                                "  velocity   72 +- 6\n"
                                "}\n"
                                "\n"
                                "channel lead { mixer 2 }\n"
                                "\n"
                                "voicing warm {\n"
                                "  size    4 voices\n"
                                "  spread  drop2\n"
                                "  motion  smooth\n"
                                "}\n"
                                "\n"
                                "rhythm pulse   { 1/4 1/4 1/2 }\n"
                                "rhythm running { 1/8 x4 - 1/8t ~ 1/2 }\n"
                                "\n"
                                "harmony lament {\n"
                                "  key F minor\n"
                                "  i x2 | bVI | bVII | i^1 x2 | iv | V7/iv\n"
                                "}\n"
                                "\n"
                                "section verse {\n"
                                "  length 8 bars\n"
                                "  harmony lament\n"
                                "  part pad {\n"
                                "    chords with warm\n"
                                "    rhythm { 1/2 1/2 }\n"
                                "  }\n"
                                "  part lead {\n"
                                "    melody {\n"
                                "      rhythm  pulse\n"
                                "      contour arch\n"
                                "      mute    1 of 4\n"
                                "    }\n"
                                "  }\n"
                                "}\n"
                                "\n"
                                "arrangement {\n"
                                "  verse\n"
                                "  verse x2\n"
                                "  verse as verse_b { part lead { variance 0.5 } }\n"
                                "  verse x2 identical\n"
                                "}\n";

const Block* findBlock (const Document& document, std::string_view keyword,
                        std::string_view name = {})
{
    for (const auto& block : document.blocks)
        if (block.keyword == keyword && (name.empty() || block.name() == name))
            return &block;

    return nullptr;
}

const Statement* findStatement (const Block& block, std::string_view key)
{
    for (const auto& statement : block.statements)
        if (statement.key == key)
            return &statement;

    return nullptr;
}

} // namespace

TEST_CASE ("a whole score parses into the blocks it declares", "[score][parser]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    INFO (renderAll (bag, source, "amber.score"));
    REQUIRE_FALSE (bag.hasErrors());

    // Nine blocks: nothing swallowed by a neighbour, nothing invented.
    REQUIRE (document.blocks.size() == 9);

    REQUIRE (findBlock (document, "song") != nullptr);
    REQUIRE (findBlock (document, "channel", "pad") != nullptr);
    REQUIRE (findBlock (document, "channel", "lead") != nullptr);
    REQUIRE (findBlock (document, "voicing", "warm") != nullptr);
    REQUIRE (findBlock (document, "rhythm", "pulse") != nullptr);
    REQUIRE (findBlock (document, "harmony", "lament") != nullptr);
    REQUIRE (findBlock (document, "section", "verse") != nullptr);
    REQUIRE (findBlock (document, "arrangement") != nullptr);
}

TEST_CASE ("a statement keeps its values raw for the resolver", "[score][parser]")
{
    // The parser has no opinion about what `72 +- 6` means. Three tokens, in
    // order, is the whole contract - a parser that knew the shape of every
    // value would be a second copy of the schema.
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    const auto* pad = findBlock (document, "channel", "pad");
    REQUIRE (pad != nullptr);

    const auto* velocity = findStatement (*pad, "velocity");
    REQUIRE (velocity != nullptr);
    REQUIRE (velocity->values.size() == 3);
    REQUIRE (velocity->values[0].text == "72");
    REQUIRE (velocity->values[1].kind == TokenKind::plusMinus);
    REQUIRE (velocity->values[2].text == "6");

    const auto* range = findStatement (*pad, "range");
    REQUIRE (range != nullptr);
    REQUIRE (range->values.size() == 3);
    REQUIRE (range->values[0].text == "C3");
    REQUIRE (range->values[1].kind == TokenKind::range);
    REQUIRE (range->values[2].text == "C5");
}

TEST_CASE ("a chord entry is assembled from its several tokens", "[score][parser]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    const auto* harmony = findBlock (document, "harmony", "lament");
    REQUIRE (harmony != nullptr);
    REQUIRE (harmony->chords.size() == 6);

    // `i x2` - a weight, not a length.
    REQUIRE (harmony->chords[0].root == "i");
    REQUIRE (harmony->chords[0].hasWeight);
    REQUIRE (harmony->chords[0].weight == 2);
    REQUIRE (harmony->chords[0].barCheckAfter);

    REQUIRE (harmony->chords[1].root == "bVI");
    REQUIRE_FALSE (harmony->chords[1].hasWeight);

    // `i^1 x2` - five tokens, one chord.
    REQUIRE (harmony->chords[3].root == "i");
    REQUIRE (harmony->chords[3].hasInversion);
    REQUIRE (harmony->chords[3].inversion == 1);
    REQUIRE (harmony->chords[3].weight == 2);

    // `V7/iv` - the last entry, with no bar check after it.
    REQUIRE (harmony->chords[5].root == "V7");
    REQUIRE (harmony->chords[5].of == "iv");
    REQUIRE_FALSE (harmony->chords[5].barCheckAfter);

    // A `key` statement inside a chord body is still a statement.
    REQUIRE (findStatement (*harmony, "key") != nullptr);
}

TEST_CASE ("a rhythm body reads durations, rests, ties and repeats", "[score][parser]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    const auto* pulse = findBlock (document, "rhythm", "pulse");
    REQUIRE (pulse != nullptr);
    REQUIRE (pulse->rhythm.size() == 3);
    REQUIRE (pulse->rhythm[0].text == "1/4");
    REQUIRE (pulse->rhythm[2].text == "1/2");

    // `-` and `~` carry their own length, so `1/8 x4 - 1/8t ~ 1/2` is three
    // entries: four eighths, an eighth-triplet REST, and a half-note tie.
    const auto* running = findBlock (document, "rhythm", "running");
    REQUIRE (running != nullptr);
    REQUIRE (running->rhythm.size() == 3);
    REQUIRE (running->rhythm[0].repeat == 4);
    REQUIRE (running->rhythm[0].text == "1/8");
    REQUIRE (running->rhythm[1].kind == RhythmEntry::Kind::rest);
    REQUIRE (running->rhythm[1].text == "1/8t");
    REQUIRE (running->rhythm[2].kind == RhythmEntry::Kind::tie);
    REQUIRE (running->rhythm[2].text == "1/2");
}

TEST_CASE ("an arrangement entry carries its repeat, label and overrides", "[score][parser]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    const auto* arrangement = findBlock (document, "arrangement");
    REQUIRE (arrangement != nullptr);
    REQUIRE (arrangement->arrangement.size() == 4);

    REQUIRE (arrangement->arrangement[0].section == "verse");
    REQUIRE (arrangement->arrangement[0].repeat == 1);

    REQUIRE (arrangement->arrangement[1].repeat == 2);
    REQUIRE_FALSE (arrangement->arrangement[1].identical);

    // `as` pins an instance so inserting another earlier cannot renumber it.
    REQUIRE (arrangement->arrangement[2].label == "verse_b");
    REQUIRE (arrangement->arrangement[2].overrides.size() == 1);
    REQUIRE (arrangement->arrangement[2].overrides.front().children.size() == 1);

    REQUIRE (arrangement->arrangement[3].identical);
    REQUIRE (arrangement->arrangement[3].repeat == 2);
}

TEST_CASE ("a reference is told from a declaration by the brace", "[score][parser]")
{
    // `rhythm pulse` inside a part REFERS to a rhythm; `rhythm { 1/2 1/2 }`
    // declares one inline; `rhythm pulse { ... }` at the top level declares a
    // named one. Two tokens of lookahead separate all three, so the grammar
    // needs no distinct keywords.
    const std::string_view source { kSource };
    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    const auto* verse = findBlock (document, "section", "verse");
    REQUIRE (verse != nullptr);

    // `harmony lament` with no body is a statement, not a block - and it must
    // NOT have swallowed the following `part` blocks as its header.
    REQUIRE (findStatement (*verse, "harmony") != nullptr);
    REQUIRE (verse->children.size() == 2);

    const auto& pad = verse->children.front();
    REQUIRE (pad.keyword == "part");
    REQUIRE (pad.name() == "pad");

    // The inline `rhythm { 1/2 1/2 }` is a child block with a rhythm body.
    REQUIRE (pad.children.size() == 1);
    REQUIRE (pad.children.front().keyword == "rhythm");
    REQUIRE (pad.children.front().rhythm.size() == 2);

    // While `rhythm pulse` inside melody is a statement.
    const auto& lead = verse->children.back();
    REQUIRE (lead.children.size() == 1);
    REQUIRE (lead.children.front().keyword == "melody");
    REQUIRE (findStatement (lead.children.front(), "rhythm") != nullptr);
}

TEST_CASE ("an unclosed brace is reported once, at the brace", "[score][parser]")
{
    // The cascade this replaces: an "unexpected token" on every line after the
    // brace, none of which names the cause. ONE unclosed brace here, so one
    // diagnostic - and crucially not one per following line.
    const std::string_view source = "song {\n"
                                    "  tempo 96\n"
                                    "  meter 4/4\n"
                                    "  key   F minor\n";

    DiagnosticBag bag { source };
    parse (source, bag);

    INFO (renderAll (bag, source, "x.score"));

    REQUIRE (bag.hasErrors());
    REQUIRE (bag.size() == 1);

    const auto& d = bag.all().front();
    REQUIRE (d.code == "E104");
    REQUIRE (d.primary.textIn (source) == "{");
    REQUIRE (d.related.size() == 1);
}

TEST_CASE ("two unclosed braces are two facts, not a cascade", "[score][parser]")
{
    // Each brace that was never closed is its own problem, and each is named
    // at the brace. What must NOT happen is a diagnostic per line between them.
    const std::string_view source = "song {\n"
                                    "  tempo 96\n"
                                    "\n"
                                    "channel pad {\n"
                                    "  mixer 1\n";

    DiagnosticBag bag { source };
    parse (source, bag);

    INFO (renderAll (bag, source, "x.score"));

    auto unclosed = 0;

    for (const auto& d : bag.all())
        if (d.code == "E104")
        {
            ++unclosed;
            REQUIRE (d.primary.textIn (source) == "{");
        }

    REQUIRE (unclosed == 2);
    REQUIRE (bag.size() == 2);
}

TEST_CASE ("one bad statement costs one line", "[score][parser]")
{
    const std::string_view source = "song {\n"
                                    "  tempo 96\n"
                                    "  ]]] nonsense\n"
                                    "  meter 4/4\n"
                                    "}\n";

    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    INFO (renderAll (bag, source, "x.score"));

    REQUIRE (bag.hasErrors());
    REQUIRE (document.blocks.size() == 1);

    // The statements on either side of the bad line both survived.
    const auto& song = document.blocks.front();
    REQUIRE (findStatement (song, "tempo") != nullptr);
    REQUIRE (findStatement (song, "meter") != nullptr);
}

TEST_CASE ("a broken block costs at most that block", "[score][parser]")
{
    // The top-level resync set at work: a keyword at the start of a line always
    // begins a new block, so the file after the damage still parses.
    const std::string_view source = "song {\n"
                                    "  tempo 96\n"
                                    "}\n"
                                    "@@@ !!! ???\n"
                                    "channel pad { mixer 1 }\n";

    DiagnosticBag bag { source };
    const auto document = parse (source, bag);

    INFO (renderAll (bag, source, "x.score"));

    REQUIRE (bag.hasErrors());
    REQUIRE (findBlock (document, "song") != nullptr);
    REQUIRE (findBlock (document, "channel", "pad") != nullptr);
}

TEST_CASE ("a chord shape that is not one is named", "[score][parser]")
{
    const std::string_view source = "harmony h {\n  i^ | V7/ | 5\n}\n";

    DiagnosticBag bag { source };
    parse (source, bag);

    INFO (renderAll (bag, source, "x.score"));
    REQUIRE (bag.hasErrors());
}

TEST_CASE ("the parser terminates and stays in bounds on any input", "[score][parser]")
{
    // Deterministic, not a fuzzer: a fixed seed and a fixed corpus, so a failure
    // here is reproducible rather than a story about a build that once went red.
    //
    // The invariants are the ones the editor's overlay depends on: it always
    // finishes, and every range it reports is inside the document.
    const std::string source { kSource };

    auto check = [] (const std::string& text)
    {
        DiagnosticBag bag { text };
        const auto document = parse (text, bag);

        for (const auto& d : bag.all())
        {
            REQUIRE (d.primary.begin <= d.primary.end);
            REQUIRE (d.primary.end <= (std::uint32_t) text.size());

            for (const auto& related : d.related)
                REQUIRE (related.range.end <= (std::uint32_t) text.size());
        }

        return document.blocks.size();
    };

    // Every prefix. A half-typed file is the normal state in an editor.
    for (std::size_t n = 0; n <= source.size(); ++n)
        check (source.substr (0, n));

    // Deliberate pathologies, including the one that recurses per byte.
    check (std::string (400, '{'));
    check (std::string (400, '}'));
    check (std::string (200, '|'));
    check ("harmony h {" + std::string (200, '^') + "}");
    check ("song {" + std::string (200, 'x') + "}");
    check ("arrangement { " + std::string (100, 'a') + " as }");

    std::mt19937 random { 20260902 };

    for (int i = 0; i < 3000; ++i)
    {
        auto mutated = source;

        const auto kind = random() % 3;
        const auto at = random() % mutated.size();

        if (kind == 0)
        {
            mutated.erase (at, 1);
        }
        else if (kind == 1)
        {
            static const char bytes[] = "{}[]|^/~-.\"#x0123t";
            mutated[at] = bytes[random() % (sizeof (bytes) - 1)];
        }
        else
        {
            const auto length = std::min<std::size_t> (mutated.size() - at, 1 + random() % 40);
            mutated.insert (at, mutated.substr (at, length));
        }

        check (mutated);
    }
}
