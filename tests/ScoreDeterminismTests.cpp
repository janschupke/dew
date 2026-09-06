#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "lang/Compile.h"

using namespace dew::lang;

namespace
{

/** A score with three sections that all use randomness, so an edit to one has
    somewhere to leak into.
*/
std::string scoreWith (const std::string& verseVariance = "0.5",
                       const std::string& arrangement = "  intro\n  verse\n  outro\n",
                       const std::string& seed = "0x51A9", const std::string& tempo = "120")
{
    return "song {\n"
           "  title \"T\"\n"
           "  tempo "
           + tempo
           + "\n"
             "  meter 4/4\n"
             "  key   C major\n"
             "  seed  "
           + seed
           + "\n"
             "}\n"
             "channel lead {\n"
             "  mixer 1\n"
             "  range C4..C6\n"
             "}\n"
             "rhythm pulse { 1/4 }\n"
             "harmony h { I | vi | IV | V }\n"
             "section intro {\n"
             "  length 4 bars\n"
             "  harmony h\n"
             "  part lead {\n"
             "    melody {\n"
             "      rhythm   pulse\n"
             "      variance 0.5\n"
             "    }\n"
             "  }\n"
             "}\n"
             "section verse {\n"
             "  length 4 bars\n"
             "  harmony h\n"
             "  part lead {\n"
             "    melody {\n"
             "      rhythm   pulse\n"
             "      variance "
           + verseVariance
           + "\n"
             "    }\n"
             "  }\n"
             "}\n"
             "section outro {\n"
             "  length 4 bars\n"
             "  harmony h\n"
             "  part lead {\n"
             "    melody {\n"
             "      rhythm   pulse\n"
             "      variance 0.5\n"
             "    }\n"
             "  }\n"
             "}\n"
             "arrangement {\n"
           + arrangement + "}\n";
}

Score compileOk (const std::string& source)
{
    const auto result = compile (source);
    INFO (result.report (source, "t.score"));
    REQUIRE (result.ok());
    return *result.score;
}

/** The pitches of the pattern named `name`, so one section can be compared
    across two compiles without depending on pattern ORDER.
*/
std::vector<int> pitchesOf (const Score& score, const std::string& name)
{
    for (const auto& pattern : score.patterns)
        if (pattern.name == name)
        {
            std::vector<int> pitches;

            for (const auto& note : pattern.notes)
                pitches.push_back (note.pitch);

            return pitches;
        }

    return {};
}

/** Everything that would go in a golden file: the shape and every note. */
std::string fingerprint (const Score& score)
{
    std::string out;
    out += score.title + "|" + std::to_string (score.tempoBpm) + "|"
           + std::to_string (score.stepsPerBeat) + "|" + std::to_string (score.barsInSong) + "\n";

    for (const auto& pattern : score.patterns)
    {
        out += "P " + pattern.name + " " + std::to_string (pattern.lengthSteps) + "\n";

        for (const auto& note : pattern.notes)
            out += "  " + std::to_string (note.track) + " " + std::to_string (note.startStep) + " "
                   + std::to_string (note.lengthSteps) + " " + std::to_string (note.pitch) + " "
                   + std::to_string ((int) (note.velocity * 1000.0f)) + "\n";
    }

    for (const auto& clip : score.clips)
        out += "C " + std::to_string (clip.pattern) + " " + std::to_string (clip.startBar) + " "
               + std::to_string (clip.lengthBars) + "\n";

    return out;
}

} // namespace

TEST_CASE ("compiling the same source twice gives the same score", "[score][determinism]")
{
    const auto source = scoreWith();

    REQUIRE (fingerprint (compileOk (source)) == fingerprint (compileOk (source)));
}

TEST_CASE ("editing one section leaves every other section alone", "[score][determinism]")
{
    // THE invariant that forces the design. Draws are stateless and keyed on a
    // structural path, so changing how many random values one decision consumes
    // cannot shift any other decision. A shared stream would mean editing
    // `verse` silently rewrote `intro` and `outro` - the classic
    // procedural-generation regression, invisible until someone notices the
    // song changed.
    const auto before = compileOk (scoreWith ("0.5"));
    const auto after = compileOk (scoreWith ("0.9"));

    REQUIRE_FALSE (pitchesOf (before, "verse").empty());

    // The edited section did change, or the test proves nothing.
    REQUIRE (pitchesOf (before, "verse") != pitchesOf (after, "verse"));

    // And the others did not.
    REQUIRE (pitchesOf (before, "intro") == pitchesOf (after, "intro"));
    REQUIRE (pitchesOf (before, "outro") == pitchesOf (after, "outro"));
}

TEST_CASE ("reordering the arrangement moves clips but not notes", "[score][determinism]")
{
    const auto before = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n"));
    const auto after = compileOk (scoreWith ("0.5", "  outro\n  intro\n  verse\n"));

    for (const auto* name : { "intro", "verse", "outro" })
    {
        INFO ("section " << name);
        REQUIRE_FALSE (pitchesOf (before, name).empty());
        REQUIRE (pitchesOf (before, name) == pitchesOf (after, name));
    }
}

TEST_CASE ("appending an instance changes nothing before it", "[score][determinism]")
{
    const auto before = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n"));
    const auto after = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n  intro\n"));

    // The first three instances are untouched; only a fourth was added.
    REQUIRE (pitchesOf (before, "intro") == pitchesOf (after, "intro"));
    REQUIRE (pitchesOf (before, "verse") == pitchesOf (after, "verse"));
    REQUIRE (pitchesOf (before, "outro") == pitchesOf (after, "outro"));

    REQUIRE (after.clips.size() == before.clips.size() + 1);
}

TEST_CASE ("whitespace and comments change nothing at all", "[score][determinism]")
{
    // A decision site's identity is its STRUCTURAL path, never its byte offset.
    // If it were the offset, inserting a blank line would reshuffle the song -
    // and that is the bug a well-meaning refactor introduces if it is not
    // written down.
    const auto plain = scoreWith();

    auto spaced = std::string ("// a comment at the top\n\n\n") + plain;
    spaced += "\n// and one at the bottom\n";

    REQUIRE (fingerprint (compileOk (plain)) == fingerprint (compileOk (spaced)));
}

TEST_CASE ("changing the tempo or the title changes no note", "[score][determinism]")
{
    const auto slow = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n", "0x51A9", "120"));
    const auto fast = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n", "0x51A9", "160"));

    for (const auto* name : { "intro", "verse", "outro" })
    {
        INFO ("section " << name);
        REQUIRE (pitchesOf (slow, name) == pitchesOf (fast, name));
    }
}

TEST_CASE ("changing the seed changes the music", "[score][determinism]")
{
    const auto a = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n", "0x51A9"));
    const auto b = compileOk (scoreWith ("0.5", "  intro\n  verse\n  outro\n", "0xBEEF"));

    auto changed = 0;

    for (const auto* name : { "intro", "verse", "outro" })
        if (pitchesOf (a, name) != pitchesOf (b, name))
            ++changed;

    INFO ("sections that changed: " << changed);
    REQUIRE (changed > 0);
}

TEST_CASE ("an identical repeat really is identical", "[score][determinism]")
{
    const auto score = compileOk (scoreWith ("0.5", "  verse x3 identical\n"));

    REQUIRE (score.clips.size() == 3);
    REQUIRE (score.patterns.size() == 1);

    // All three clips point at the one pattern, so they cannot differ.
    for (const auto& clip : score.clips)
        REQUIRE (clip.pattern == score.clips.front().pattern);
}

TEST_CASE ("a repeat without `identical` differs, and pins its own instances",
           "[score][determinism]")
{
    const auto twice = compileOk (scoreWith ("0.5", "  verse x2\n"));

    REQUIRE (twice.clips.size() == 2);
    REQUIRE (twice.patterns.size() == 2);
    REQUIRE (pitchesOf (twice, "verse") != pitchesOf (twice, "verse 2"));

    // Adding a THIRD repeat leaves the first two exactly as they were: an
    // instance is numbered by occurrence of its section's name, so appending
    // cannot renumber what came before.
    const auto thrice = compileOk (scoreWith ("0.5", "  verse x3\n"));

    REQUIRE (pitchesOf (twice, "verse") == pitchesOf (thrice, "verse"));
    REQUIRE (pitchesOf (twice, "verse 2") == pitchesOf (thrice, "verse 2"));
}

TEST_CASE ("`as` pins an instance against renumbering", "[score][determinism]")
{
    // The honest residual: inserting an instance in the MIDDLE renumbers the
    // ones after it, and their choices re-roll. `as <label>` is the escape
    // hatch, and this is what it buys.
    const auto before = compileOk (scoreWith ("0.5", "  verse\n  verse as tail\n"));
    const auto after = compileOk (scoreWith ("0.5", "  verse\n  intro\n  verse as tail\n"));

    // The labelled instance is unmoved by the insertion...
    REQUIRE_FALSE (pitchesOf (before, "tail").empty());
    REQUIRE (pitchesOf (before, "tail") == pitchesOf (after, "tail"));

    // ...and so is the unlabelled one before the insertion point.
    REQUIRE (pitchesOf (before, "verse") == pitchesOf (after, "verse"));
}

TEST_CASE ("variance zero makes the seed irrelevant", "[score][determinism]")
{
    // At zero the generator is a pure argmin with nothing random in it, so two
    // different seeds must agree exactly. That is what makes the knob honest:
    // "deterministic" is not a promise about one seed, it is the absence of a
    // draw at all.
    const auto a = compileOk (scoreWith ("0.0", "  verse\n", "0x1111"));
    const auto b = compileOk (scoreWith ("0.0", "  verse\n", "0x2222"));

    REQUIRE_FALSE (pitchesOf (a, "verse").empty());
    REQUIRE (pitchesOf (a, "verse") == pitchesOf (b, "verse"));
}

TEST_CASE ("compiling many times over a running process never wavers", "[score][determinism]")
{
    // Compiling twice was not enough, and this is the test that says why.
    //
    // A mute budget sorted its candidates with a comparator that searched the
    // very range being sorted, to recover an index into a parallel array of
    // tiebreaks. That answer changed as the sort moved elements about, so the
    // comparator was not a consistent ordering - and an inconsistent comparator
    // does not merely sort wrongly, it lets the algorithm run past the end of
    // the range. What it read there was whatever the process happened to have
    // in that memory, so the same score compiled to different music about one
    // run in four.
    //
    // Two compiles in a row read the same rubbish and agreed. Twenty, with a
    // heap that has moved on between them, do not.
    const auto source = scoreWith ("0.5", "  intro\n  verse x2\n  outro\n");
    const auto expected = fingerprint (compileOk (source));

    for (auto i = 0; i < 20; ++i)
    {
        INFO ("compile " << i);

        // Something allocated and freed between compiles, so a read of freed
        // or uninitialised memory has somewhere different to land each time.
        std::vector<std::string> churn;

        for (auto j = 0; j < 64; ++j)
            churn.emplace_back ((std::size_t) (32 + j), (char) ('a' + (j % 26)));

        REQUIRE (fingerprint (compileOk (source)) == expected);
    }
}

namespace
{

/** A score whose line ends on a chosen chord tone. */
std::string withCadence (const std::string& cadence, const std::string& arrangement = "  verse\n",
                         const std::string& seed = "0x51A9")
{
    return "song {\n"
           "  title \"T\"\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "  seed  "
           + seed
           + "\n"
             "}\n"
             "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
             "rhythm pulse { 1/4 }\n"
             "harmony h { I | vi | IV | V }\n"
             "section verse {\n"
             "  length 4 bars\n"
             "  harmony h\n"
             "  part lead {\n"
             "    melody {\n"
             "      rhythm   pulse\n"
             "      variance 0.5\n"
           + cadence
           + "    }\n"
             "  }\n"
             "}\n"
             "arrangement {\n"
           + arrangement + "}\n";
}

/** The pitch class the last note of a pattern lands on. */
int endsOn (const Score& score, const std::string& pattern)
{
    for (const auto& p : score.patterns)
        if (p.name == pattern && ! p.notes.empty())
            return ((p.notes.back().pitch % 12) + 12) % 12;

    return -1;
}

} // namespace

TEST_CASE ("a cadence lands the line on the chord tone it names", "[score][determinism][cadence]")
{
    // The progression ends on V, which in C major is G: root 7, third 11,
    // fifth 2. Naming the tone has to be enough to land on it.
    REQUIRE (endsOn (compileOk (withCadence ("      cadence 1\n")), "verse") == 7);
    REQUIRE (endsOn (compileOk (withCadence ("      cadence 3\n")), "verse") == 11);
    REQUIRE (endsOn (compileOk (withCadence ("      cadence 5\n")), "verse") == 2);

    // And without one the line ends wherever the scoring put it, which is the
    // point of the knob existing at all.
    REQUIRE (endsOn (compileOk (withCadence ("")), "verse") >= 0);
}

TEST_CASE ("a chosen cadence is one draw per instance", "[score][determinism][cadence]")
{
    // "changing the ending note, based on the seed" - the same score, the same
    // seed, twice, and then a different seed.
    const auto choice = "      cadence choose [1 3 5] per instance\n";

    const auto once = compileOk (withCadence (choice));
    REQUIRE (endsOn (once, "verse") == endsOn (compileOk (withCadence (choice)), "verse"));

    // Three instances, and each gets its OWN draw - the repeats differ.
    const auto thrice = compileOk (withCadence (choice, "  verse x3\n"));
    REQUIRE (thrice.patterns.size() == 3);

    std::set<int> endings;

    for (const auto& pattern : thrice.patterns)
        if (! pattern.notes.empty())
            endings.insert (((pattern.notes.back().pitch % 12) + 12) % 12);

    INFO ("endings: " << endings.size());
    REQUIRE (endings.size() > 1);

    // Every one of them is one of the three that was offered - C major's V is
    // G, so root 7, third 11, fifth 2.
    for (const auto ending : endings)
    {
        INFO ("ending pitch class " << ending);
        REQUIRE ((ending == 7 || ending == 11 || ending == 2));
    }
}

TEST_CASE ("`per song` draws once for the whole song", "[score][determinism][cadence]")
{
    // The scope IS the identity of the draw: at song scope three instances of
    // the same section end the same way, which is what makes the scope worth
    // writing rather than a synonym for "random".
    const auto score = compileOk (
        withCadence ("      cadence choose [1 3 5] per song\n", "  verse x3\n"));

    REQUIRE (score.patterns.size() == 3);

    std::set<int> endings;

    for (const auto& pattern : score.patterns)
        if (! pattern.notes.empty())
            endings.insert (((pattern.notes.back().pitch % 12) + 12) % 12);

    REQUIRE (endings.size() == 1);
}

TEST_CASE ("a velocity scope changes how often the jitter is re-drawn", "[score][determinism]")
{
    const auto scoreWithScope = [] (const std::string& scope)
    {
        return compileOk ("song {\n  tempo 120\n  meter 4/4\n  key C major\n  seed 7\n}\n"
                          "channel pad {\n  mixer 1\n  range C3..C5\n  velocity 80 +- 20"
                          + scope
                          + "\n}\n"
                            "voicing warm { size 3 voices }\n"
                            "rhythm pulse { 1/4 }\n"
                            "harmony h { I | V }\n"
                            "section verse {\n  length 2 bars\n  harmony h\n"
                            "  part pad {\n    chords with warm\n    rhythm pulse\n  }\n}\n"
                            "arrangement {\n  verse\n}\n");
    };

    const auto velocitiesOf = [] (const Score& score)
    {
        std::set<int> values;

        for (const auto& pattern : score.patterns)
            for (const auto& note : pattern.notes)
                values.insert ((int) (note.velocity * 1000.0f));

        return values;
    };

    // Per note is the default and gives many different values...
    REQUIRE (velocitiesOf (scoreWithScope ("")).size() > 2);

    // ...and per instance gives exactly one, for the whole instance.
    REQUIRE (velocitiesOf (scoreWithScope (" per instance")).size() == 1);

    // Per bar sits between them: two bars, so at most two values.
    const auto perBar = velocitiesOf (scoreWithScope (" per bar"));
    INFO ("distinct velocities per bar: " << perBar.size());
    REQUIRE (perBar.size() <= 2);
    REQUIRE_FALSE (perBar.empty());
}
