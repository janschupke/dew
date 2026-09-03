#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "lang/Completion.h"

using namespace dew::lang;

namespace
{

/** A score with a caret in it, written as `$`.

    The offset is where the marker was, and the marker is removed - so a test
    reads as the thing somebody is actually doing rather than as an arithmetic
    puzzle about byte offsets.

    NOT `|`, which is the obvious choice and was wrong: a bar-line assertion is
    spelled `|`, so a fixture containing `i | bVI` had its caret found inside
    its own harmony and every name test collapsed into the chord list.
*/
CompletionResult at (const std::string& sourceWithCaret)
{
    const auto caret = sourceWithCaret.find ('$');
    REQUIRE (caret != std::string::npos);

    auto source = sourceWithCaret;
    source.erase (caret, 1);

    return completionsAt (source, (std::uint32_t) caret);
}

std::vector<std::string> textsOf (const CompletionResult& result)
{
    std::vector<std::string> out;

    for (const auto& item : result.items)
        out.push_back (item.text);

    return out;
}

bool offers (const CompletionResult& result, const std::string& text)
{
    const auto items = textsOf (result);
    return std::find (items.begin(), items.end(), text) != items.end();
}

/** A file with something declared in it, so name completion has names. */
std::string declared (const std::string& tail)
{
    return
        "song {\n"
        "  tempo 120\n"
        "  meter 4/4\n"
        "  key   F minor\n"
        "}\n"
        "channel pad { mixer 1 }\n"
        "channel lead { mixer 2 }\n"
        "voicing warm { size 4 voices }\n"
        "rhythm pulse { 1/4 }\n"
        "harmony lament { i | bVI }\n"
        "section verse {\n  length 4 bars\n  harmony lament\n}\n"
        + tail;
}

} // namespace

TEST_CASE ("the top level offers the block keywords, and only those",
           "[score][completion]")
{
    const auto result = at ("$");

    REQUIRE (result.block == BlockKind::unknown);
    REQUIRE (textsOf (result) == std::vector<std::string> {
        "song", "channel", "voicing", "rhythm", "harmony", "section", "arrangement" });
}

TEST_CASE ("a prefix filters, and says what it replaces", "[score][completion]")
{
    const auto result = at ("cha$");

    REQUIRE (textsOf (result) == std::vector<std::string> { "channel" });

    // The editor replaces what was typed rather than appending to it, or
    // accepting `channel` after `cha` would spell `chachannel`.
    REQUIRE (result.replacing.begin == 0);
    REQUIRE (result.replacing.end == 3);
}

TEST_CASE ("inside a block, the keys are that block's", "[score][completion]")
{
    const auto song = at ("song {\n  $\n}\n");

    REQUIRE (song.block == BlockKind::song);
    REQUIRE (textsOf (song) == std::vector<std::string> {
        "title", "tempo", "meter", "grid", "key", "seed" });

    // And a channel's are a channel's, which is the whole claim.
    const auto channel = at ("channel pad {\n  $\n}\n");

    REQUIRE (channel.block == BlockKind::channel);
    REQUIRE (offers (channel, "mixer"));
    REQUIRE (offers (channel, "range"));
    REQUIRE_FALSE (offers (channel, "tempo"));
}

TEST_CASE ("a block that nests offers its children too", "[score][completion]")
{
    const auto section = at ("section verse {\n  $\n}\n");

    REQUIRE (section.block == BlockKind::section);
    REQUIRE (offers (section, "length"));
    REQUIRE (offers (section, "harmony"));
    REQUIRE (offers (section, "part"));

    const auto part = at ("section verse {\n  part pad {\n    $\n  }\n}\n");

    REQUIRE (part.block == BlockKind::part);
    REQUIRE (offers (part, "chords"));
    REQUIRE (offers (part, "melody"));
    REQUIRE_FALSE (offers (part, "length"));
}

TEST_CASE ("after a key, the values are that key's", "[score][completion]")
{
    SECTION ("a closed set")
    {
        const auto result = at ("voicing warm {\n  spread $\n}\n");

        REQUIRE (textsOf (result) == std::vector<std::string> {
            "close", "open", "drop2", "drop3", "shell", "rootless" });
    }

    SECTION ("a contour")
    {
        const auto result = at ("section s {\n  part p {\n    melody {\n"
                                "      contour $\n    }\n  }\n}\n");

        REQUIRE (textsOf (result) == std::vector<std::string> {
            "arch", "rise", "fall", "flat", "wave" });
    }

    SECTION ("a key is two stages")
    {
        // `key ` wants a root and `key F ` wants a mode. One key, two positions,
        // and getting this wrong offers "minor" where only "F" can go.
        const auto root = at ("song {\n  key $\n}\n");
        REQUIRE (offers (root, "F"));
        REQUIRE (offers (root, "Bb"));
        REQUIRE_FALSE (offers (root, "minor"));

        const auto mode = at ("song {\n  key F $\n}\n");
        REQUIRE (offers (mode, "minor"));
        REQUIRE (offers (mode, "harmonic-minor"));
        REQUIRE_FALSE (offers (mode, "F"));
    }
}

TEST_CASE ("a reference offers what this file declares", "[score][completion]")
{
    SECTION ("rhythms")
    {
        const auto result = at (declared ("section s {\n  part pad {\n    rhythm $\n  }\n}\n"));
        REQUIRE (textsOf (result) == std::vector<std::string> { "pulse" });
    }

    SECTION ("voicings, after `with`")
    {
        const auto result = at (declared ("section s {\n  part pad {\n"
                                          "    chords with $\n  }\n}\n"));
        REQUIRE (textsOf (result) == std::vector<std::string> { "warm" });
    }

    SECTION ("harmonies")
    {
        const auto result = at (declared ("section s {\n  harmony $\n}\n"));
        REQUIRE (textsOf (result) == std::vector<std::string> { "lament" });
    }

    SECTION ("the channel a part is for")
    {
        const auto result = at (declared ("section s {\n  part $\n}\n"));
        REQUIRE (textsOf (result) == std::vector<std::string> { "pad", "lead" });
    }

    SECTION ("the sections an arrangement may name")
    {
        const auto result = at (declared ("arrangement {\n  $\n}\n"));
        REQUIRE (textsOf (result) == std::vector<std::string> { "verse" });
    }
}

TEST_CASE ("chords are offered in the key that is written", "[score][completion]")
{
    // The completion that makes the language feel like it knows what you are
    // doing. In F minor the first offer is `i`, not `I`.
    const auto minor = at ("song {\n  key F minor\n}\nharmony h {\n  $\n}\n");

    REQUIRE (minor.block == BlockKind::harmony);
    REQUIRE (minor.items.front().text == "i");
    REQUIRE (offers (minor, "bVI"));
    REQUIRE (offers (minor, "bVII"));

    const auto major = at ("song {\n  key C major\n}\nharmony h {\n  $\n}\n");

    REQUIRE (major.items.front().text == "I");
    REQUIRE (offers (major, "vi"));
    REQUIRE (offers (major, "V7/vi"));

    // Not the same list, or the key was never read.
    REQUIRE (textsOf (minor) != textsOf (major));
}

TEST_CASE ("a rhythm offers durations", "[score][completion]")
{
    const auto result = at ("rhythm r {\n  $\n}\n");

    REQUIRE (result.block == BlockKind::rhythm);
    REQUIRE (offers (result, "1/4"));
    REQUIRE (offers (result, "1/8t"));
    REQUIRE (offers (result, "-"));
    REQUIRE (offers (result, "~"));
}

TEST_CASE ("an arrangement offers repeats after a section", "[score][completion]")
{
    const auto result = at (declared ("arrangement {\n  verse $\n}\n"));

    REQUIRE (offers (result, "x2"));
    REQUIRE (offers (result, "identical"));
    REQUIRE (offers (result, "as"));
}

TEST_CASE ("completion works in a file that does not compile",
           "[score][completion]")
{
    // The only file anybody is ever editing. Names are collected by a resolve
    // that runs whether or not the parse produced errors, so a half-written
    // section below does not take the channel list away.
    const auto result = at (
        "channel pad { mixer 1 }\n"
        "section verse {\n"
        "  part $\n"
        "  length\n"          // missing its value
        "  nonsense here\n"   // not a key at all
        "\n");                // and never closed

    REQUIRE (textsOf (result) == std::vector<std::string> { "pad" });
}

TEST_CASE ("nothing is offered inside a comment or a string",
           "[score][completion]")
{
    // Offering `section` in the middle of a sentence is not help, it is
    // interference.
    REQUIRE (at ("// a note about the cho$rus\n").items.empty());
    REQUIRE (at ("song {\n  title \"my $song\"\n}\n").items.empty());

    // But the line after the comment is ordinary again.
    REQUIRE_FALSE (at ("// a note\n$\n").items.empty());
}

TEST_CASE ("an unknown key offers nothing rather than guessing",
           "[score][completion]")
{
    // `mixor 1` is a typo, not a key, and inventing values for it would be
    // pretending the schema said something it did not.
    REQUIRE (at ("channel pad {\n  mixor $\n}\n").items.empty());
}

TEST_CASE ("every candidate carries what it is", "[score][completion]")
{
    // The `doc` strings in the schema are the language's reference manual, and
    // showing them here is what stops the manual and the grammar drifting.
    const auto result = at ("song {\n  $\n}\n");

    for (const auto& item : result.items)
    {
        INFO ("item " << item.text);
        REQUIRE_FALSE (item.detail.empty());
        REQUIRE (item.kind == CompletionKind::key);
    }
}

TEST_CASE ("a chord is offered spelled out", "[score][completion]")
{
    // A list of roman numerals is a list of symbols; a list of numerals with
    // their notes beside them is one you can choose from.
    const auto result = at ("song {\n  key A minor\n}\nharmony h {\n  $\n}\n");

    const auto detailOf = [&result] (const std::string& text)
    {
        for (const auto& item : result.items)
            if (item.text == text)
                return item.detail;

        return std::string ("<not offered>");
    };

    REQUIRE (detailOf ("i") == "Am");
    REQUIRE (detailOf ("iv") == "Dm");
    REQUIRE (detailOf ("bVI") == "F");
    REQUIRE (detailOf ("bVII") == "G");
    REQUIRE (detailOf ("V7") == "E7");

    // An inversion and a tonicisation have to be taken apart before they can be
    // spelled - `^` and `/` are not part of the root - or these are the two
    // entries in the list with nothing beside them.
    REQUIRE (detailOf ("i^1") == "Am");
    // "A7/iv" rather than "A7": the compiler's own label keeps the tonicisation
    // it is for, which says more than the chord alone. In A minor, iv is D
    // minor and its dominant is A7.
    REQUIRE (detailOf ("V7/iv") == "A7/iv");

    // EVERY offered chord has to resolve, which is what a non-empty spelling
    // means. Completion that offers what the compiler rejects is worse than
    // none: the first draft offered `vii°` with the typographic degree sign,
    // and the language spells that `viidim`.
    for (const auto& item : result.items)
    {
        INFO ("item " << item.text);
        REQUIRE_FALSE (item.detail.empty());
    }
}

TEST_CASE ("every chord completion compiles", "[score][completion]")
{
    // The same claim in the other key, so the major list is covered too.
    for (const auto* source : { "song {\n  key C major\n}\nharmony h {\n  $\n}\n",
                                "song {\n  key F minor\n}\nharmony h {\n  $\n}\n" })
    {
        const auto result = at (source);
        REQUIRE (result.items.size() > 10);

        for (const auto& item : result.items)
        {
            if (item.kind != CompletionKind::value)
                continue;

            INFO ("chord " << item.text << " in " << source);
            REQUIRE_FALSE (item.detail.empty());
        }
    }
}

TEST_CASE ("a nested block is never offered at the top level",
           "[score][completion]")
{
    // The schema declares which blocks belong at the top level. A hand-written
    // exclusion list here offered `counterpoint` as a seventh top-level keyword
    // the day one was added, and it would have done it again for the next one.
    const auto top = textsOf (at ("$"));

    for (const auto* nested : { "part", "melody", "counterpoint", "overrides" })
    {
        INFO ("nested block " << nested);
        REQUIRE (std::find (top.begin(), top.end(), nested) == top.end());
    }

    // And every block the schema calls top level IS offered.
    for (const auto& spec : schema())
        if (spec.topLevel)
        {
            INFO ("top-level block " << nameOf (spec.kind));
            REQUIRE (std::find (top.begin(), top.end(), nameOf (spec.kind)) != top.end());
        }
}
