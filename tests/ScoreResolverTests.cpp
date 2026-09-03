#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>

#include "lang/Parser.h"
#include "lang/Resolver.h"

using namespace dew::lang;

namespace
{

/** A score that resolves cleanly, for tests that want to change one thing. */
std::string wellFormed (const std::string& extra = {})
{
    return
        "song {\n"
        "  tempo 96 bpm\n"
        "  meter 4/4\n"
        "  key   F minor\n"
        "  seed  0x51A9\n"
        "}\n"
        "channel pad { mixer 1 }\n"
        "voicing warm { size 4 voices }\n"
        "rhythm pulse { 1/4 1/4 1/2 }\n"
        "harmony lament { i x2 | bVI | bVII }\n"
        "section verse {\n"
        "  length 8 bars\n"
        "  harmony lament\n"
        "  part pad {\n"
        "    chords with warm\n"
        "  }\n"
        "}\n"
        "arrangement {\n"
        "  verse\n"
        "}\n"
        + extra;
}

struct Resolved
{
    DiagnosticBag bag;
    SymbolTable symbols;
    Model model;
    std::string text;
};

/** Parses and resolves, keeping the source alive - the model holds string_views
    into it, so a temporary would dangle.
*/
std::unique_ptr<Resolved> resolveText (std::string source)
{
    auto out = std::make_unique<Resolved> (Resolved { DiagnosticBag { source }, {}, {},
                                                      std::move (source) });

    // The bag was built over the moved-from string, so rebuild it over the kept one.
    out->bag = DiagnosticBag { out->text };

    const auto document = parse (out->text, out->bag);
    out->model = resolve (document, out->text, out->bag, out->symbols);

    return out;
}

/** The ci preset builds with -Wfloat-equal as an error, and Catch2's decomposer
    trips it, so a double is compared through here rather than with ==.
*/
bool same (double a, double b) { return std::abs (a - b) < 1e-9; }

bool hasCode (const DiagnosticBag& bag, std::string_view code)
{
    for (const auto& d : bag.all())
        if (d.code == code)
            return true;

    return false;
}

const Diagnostic* diagnosticWith (const DiagnosticBag& bag, std::string_view code)
{
    for (const auto& d : bag.all())
        if (d.code == code)
            return &d;

    return nullptr;
}

} // namespace

TEST_CASE ("a well-formed score resolves with nothing to say", "[score][resolver]")
{
    const auto r = resolveText (wellFormed());

    INFO (renderAll (r->bag, r->text, "x.score"));
    REQUIRE_FALSE (r->bag.hasErrors());

    REQUIRE (same (r->model.song.tempoBpm, 96.0));
    REQUIRE (r->model.song.meter.beatsPerBar == 4);
    REQUIRE (r->model.song.key.tonicPc == 5);
    REQUIRE (r->model.song.key.mode == Mode::minor);
    REQUIRE (r->model.song.seed == 0x51A9);
    REQUIRE (r->model.song.gridIsAuto);

    REQUIRE (r->model.channels.size() == 1);
    REQUIRE (r->model.voicings.size() == 1);
    REQUIRE (r->model.rhythms.size() == 1);
    REQUIRE (r->model.harmonies.size() == 1);
    REQUIRE (r->model.sections.size() == 1);
    REQUIRE (r->model.arrangement.size() == 1);

    REQUIRE (r->model.sections.front().bars == 8);
    REQUIRE (r->model.sections.front().parts.size() == 1);
    REQUIRE (r->model.sections.front().parts.front().kind == PartKind::chords);
}

TEST_CASE ("the symbol table is filled even when resolution fails",
           "[score][resolver]")
{
    // Completion in a file that does not yet compile is the only case that
    // matters, so pass one runs regardless of what pass two finds.
    const auto r = resolveText (
        "channel pad { mixer 1 }\n"
        "voicing warm { size 4 voices }\n"
        "rhythm pulse { 1/4 }\n"
        "harmony lament { i }\n"
        "section verse { !!! }\n");

    REQUIRE (r->bag.hasErrors());

    REQUIRE (r->symbols.channels == std::vector<std::string> { "pad" });
    REQUIRE (r->symbols.voicings == std::vector<std::string> { "warm" });
    REQUIRE (r->symbols.rhythms == std::vector<std::string> { "pulse" });
    REQUIRE (r->symbols.harmonies == std::vector<std::string> { "lament" });
    REQUIRE (r->symbols.sections == std::vector<std::string> { "verse" });
}

TEST_CASE ("an unknown key is named, with a suggestion when one is close",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n"
        "  tempo 96\n"
        "  meter 4/4\n"
        "  key   C major\n"
        "  tempoo 120\n"
        "}\n"
        "channel pad { mixor 1 }\n");

    const auto* unknown = diagnosticWith (r->bag, "E204");
    REQUIRE (unknown != nullptr);

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);

    REQUIRE (text.find ("`tempoo` is not a key of `song`") != std::string::npos);
    REQUIRE (text.find ("did you mean `tempo`?") != std::string::npos);
    REQUIRE (text.find ("`mixor` is not a key of `channel`") != std::string::npos);
    REQUIRE (text.find ("did you mean `mixer`?") != std::string::npos);
}

TEST_CASE ("a suggestion is offered only when it is close", "[score][resolver]")
{
    // A "did you mean" that proposes something unrelated is worse than none.
    const auto r = resolveText (
        "song {\n"
        "  tempo 96\n"
        "  meter 4/4\n"
        "  key   C major\n"
        "  wobbleflange 3\n"
        "}\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);

    REQUIRE (text.find ("`wobbleflange` is not a key") != std::string::npos);
    REQUIRE (text.find ("did you mean") == std::string::npos);
}

TEST_CASE ("a reference is reported where it is written", "[score][resolver]")
{
    // Not at any definition: the reference is the mistake, and pointing
    // elsewhere sends you to the wrong line.
    const auto r = resolveText (wellFormed() + "\n"
        "section bridge {\n"
        "  length 4 bars\n"
        "  harmony missing\n"
        "  part pad { chords with warm }\n"
        "}\n");

    const auto* d = diagnosticWith (r->bag, "E224");
    REQUIRE (d != nullptr);
    REQUIRE (d->primary.textIn (r->text) == "missing");
}

TEST_CASE ("an unknown section in the arrangement is caught at the reference",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song { tempo 96\n  meter 4/4\n  key C major\n}\n"
        "channel pad { mixer 1 }\n"
        "voicing warm { size 4 }\n"
        "harmony h { I }\n"
        "section verse {\n"
        "  length 4 bars\n"
        "  harmony h\n"
        "  part pad { chords with warm }\n"
        "}\n"
        "arrangement {\n"
        "  verssse\n"
        "}\n");

    const auto* d = diagnosticWith (r->bag, "E236");
    REQUIRE (d != nullptr);
    REQUIRE (d->primary.textIn (r->text) == "verssse");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);
    REQUIRE (text.find ("did you mean `verse`?") != std::string::npos);
}

TEST_CASE ("a name declared twice is flagged at the second one",
           "[score][resolver]")
{
    const auto r = resolveText (wellFormed() + "\nchannel pad { mixer 2 }\n");

    const auto* d = diagnosticWith (r->bag, "E201");
    REQUIRE (d != nullptr);

    // The SECOND `pad`, not the first: pointing at the first sends you to a
    // line that is probably correct.
    const auto firstPad = r->text.find ("channel pad");
    REQUIRE (d->primary.begin > (std::uint32_t) firstPad);
}

TEST_CASE ("a required key that is missing is named", "[score][resolver]")
{
    const auto r = resolveText ("song {\n  title \"x\"\n}\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);

    // ONE diagnostic naming all three, not three anchored to the same line -
    // the bag reports one error per line, so three would have become one and
    // the other two would have vanished.
    REQUIRE (hasCode (r->bag, "E206"));
    REQUIRE (text.find ("`song` needs `tempo`, `meter` and `key`") != std::string::npos);
}

TEST_CASE ("a value of the wrong shape lists what was expected",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n"
        "  tempo 96\n"
        "  meter 4/4\n"
        "  key   C major\n"
        "}\n"
        "voicing warm { spread wobbly }\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);

    REQUIRE (hasCode (r->bag, "E207"));
    REQUIRE (text.find ("one of: close, open, drop2, drop3, shell, rootless")
             != std::string::npos);
}

TEST_CASE ("an enum typo gets its own suggestion", "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "voicing warm { spread drop22 }\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);
    REQUIRE (text.find ("did you mean `drop2`?") != std::string::npos);
}

TEST_CASE ("the host's own limits are refused in the language's words",
           "[score][resolver]")
{
    SECTION ("a grid past 16 cannot be stored")
    {
        const auto r = resolveText (
            "song {\n  tempo 96\n  meter 4/4\n  key C major\n  grid 24\n}\n");

        const auto text = renderAll (r->bag, r->text, "x.score");
        INFO (text);
        REQUIRE (hasCode (r->bag, "E212"));
        REQUIRE (text.find ("stepsPerBeat runs to 16") != std::string::npos);
    }

    SECTION ("a beat unit has to be a power of two")
    {
        const auto r = resolveText (
            "song {\n  tempo 96\n  meter 4/6\n  key C major\n}\n");

        const auto text = renderAll (r->bag, r->text, "x.score");
        INFO (text);
        REQUIRE (hasCode (r->bag, "E211"));
        REQUIRE (text.find ("base-2 logarithm") != std::string::npos);
    }

    SECTION ("velocity zero is a note-off, not a quiet note")
    {
        const auto r = resolveText (
            "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
            "channel pad { velocity 0 }\n");

        const auto text = renderAll (r->bag, r->text, "x.score");
        INFO (text);
        REQUIRE (hasCode (r->bag, "E214"));
        REQUIRE (text.find ("note-off") != std::string::npos);
    }
}

TEST_CASE ("a mute budget that would silence a window is refused",
           "[score][resolver]")
{
    const auto r = resolveText (wellFormed() + "\n"
        "section quiet {\n"
        "  length 4 bars\n"
        "  harmony lament\n"
        "  part pad {\n"
        "    melody {\n"
        "      rhythm pulse\n"
        "      mute   4 of 4\n"
        "    }\n"
        "  }\n"
        "}\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);

    REQUIRE (hasCode (r->bag, "E235"));
    REQUIRE (text.find ("would silence every window") != std::string::npos);
}

TEST_CASE ("a bad chord is reported at the chord, in the harmony's key",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "harmony h { I | Vsplendid | vi }\n");

    const auto* d = diagnosticWith (r->bag, "E218");
    REQUIRE (d != nullptr);
    REQUIRE (d->primary.textIn (r->text) == "Vsplendid");

    // The chords on either side still resolved.
    REQUIRE (r->model.harmonies.size() == 1);
    REQUIRE (r->model.harmonies.front().chords.size() == 2);
}

TEST_CASE ("a harmony's own key overrides the song's for its numerals",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "harmony h {\n  key A minor\n  i | VI | VII\n}\n");

    INFO (renderAll (r->bag, r->text, "x.score"));
    REQUIRE_FALSE (r->bag.hasErrors());

    REQUIRE (r->model.harmonies.front().key.has_value());
    REQUIRE (r->model.harmonies.front().key->tonicPc == 9);
}

TEST_CASE ("every duration written anywhere is collected for the grid",
           "[score][resolver]")
{
    // The grid is a property of the WHOLE file, so the resolver has to gather
    // literals from rhythms and from chord lengths alike rather than one block
    // at a time.
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "rhythm a { 1/16 1/16 }\n"
        "rhythm b { 1/8t }\n"
        "harmony h { I 1/2 }\n");

    REQUIRE (r->model.durationUses.size() == 4);

    const auto resolved = resolveGrid (r->model.durationUses, 4);
    REQUIRE (resolved.required == 12);
}

TEST_CASE ("a rhythm repeat becomes that many steps", "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "rhythm a { 1/8 x4 - 1/4 }\n");

    INFO (renderAll (r->bag, r->text, "x.score"));

    REQUIRE (r->model.rhythms.size() == 1);

    // Four eighths, a rest, a quarter - each repeat its own step, so a mute
    // budget can land on any one of them.
    REQUIRE (r->model.rhythms.front().steps.size() == 6);
    REQUIRE (r->model.rhythms.front().steps[3].duration == Duration { 1, 8 });
    REQUIRE (r->model.rhythms.front().steps[4].isRest);
}

TEST_CASE ("a section with nothing to play is refused", "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "harmony h { I }\n"
        "section verse {\n  length 4 bars\n  harmony h\n}\n"
        "arrangement { verse }\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);
    REQUIRE (hasCode (r->bag, "E242"));
}

TEST_CASE ("a part that says nothing about what to play is refused",
           "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "channel pad { mixer 1 }\n"
        "harmony h { I }\n"
        "section verse {\n"
        "  length 4 bars\n"
        "  harmony h\n"
        "  part pad { octave 1 }\n"
        "}\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);
    REQUIRE (hasCode (r->bag, "E232"));
}

TEST_CASE ("a range that runs backwards is caught", "[score][resolver]")
{
    const auto r = resolveText (
        "song {\n  tempo 96\n  meter 4/4\n  key C major\n}\n"
        "channel pad { range C5..C3 }\n");

    const auto text = renderAll (r->bag, r->text, "x.score");
    INFO (text);
    REQUIRE (hasCode (r->bag, "E208"));
}

TEST_CASE ("an arrangement entry keeps its label, repeat and identical flag",
           "[score][resolver]")
{
    const auto r = resolveText (wellFormed() + "\n"
        "arrangement {\n"
        "  verse\n"
        "  verse x2\n"
        "  verse as verse_b\n"
        "  verse x3 identical\n"
        "}\n");

    INFO (renderAll (r->bag, r->text, "x.score"));

    // Two arrangement blocks is a second `song`-style mistake the language does
    // not currently forbid; what matters here is that the entries resolved.
    REQUIRE (r->model.arrangement.size() == 5);

    REQUIRE (r->model.arrangement[1].repeat == 1);
    REQUIRE (r->model.arrangement[2].repeat == 2);
    REQUIRE (r->model.arrangement[3].label == "verse_b");
    REQUIRE (r->model.arrangement[4].identical);
    REQUIRE (r->model.arrangement[4].repeat == 3);
}

TEST_CASE ("resolving never crashes on a broken file", "[score][resolver]")
{
    // The resolver runs on whatever the parser produced, which for a half-typed
    // file is a tree full of Error-shaped nodes. Every prefix of a real score.
    const auto full = wellFormed();

    for (std::size_t n = 0; n <= full.size(); ++n)
    {
        auto prefix = full.substr (0, n);

        DiagnosticBag bag { prefix };
        SymbolTable symbols;
        const auto document = parse (prefix, bag);
        const auto model = resolve (document, prefix, bag, symbols);

        for (const auto& d : bag.all())
            REQUIRE (d.primary.end <= (std::uint32_t) prefix.size());
    }
}
