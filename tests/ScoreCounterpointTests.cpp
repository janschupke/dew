#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "lang/Compile.h"

using namespace dew::lang;

namespace
{

/** Two voices: a melody, and a line answering it under the rules given. */
std::string duet (const std::string& rules,
                  const std::string& arrangement = "  duet\n",
                  const std::string& seed = "0xC0FFEE")
{
    return
        "song {\n"
        "  tempo 96\n"
        "  meter 4/4\n"
        "  key   D minor\n"
        "  seed  " + seed + "\n"
        "}\n"
        "channel lead {\n  mixer 1\n  range D4..D6\n}\n"
        "channel alto {\n  mixer 2\n  range D3..D5\n}\n"
        "rhythm walk { 1/4 }\n"
        "harmony h { i | bVII | bVI | V }\n"
        "section duet {\n"
        "  length 4 bars\n"
        "  harmony h\n"
        "  part lead {\n"
        "    melody {\n      rhythm walk\n      variance 0.2\n    }\n"
        "  }\n"
        "  part alto {\n"
        "    counterpoint against lead {\n"
        "      rhythm walk\n"
        + rules +
        "    }\n"
        "  }\n"
        "}\n"
        "arrangement {\n" + arrangement + "}\n";
}

CompileResult compileScore (const std::string& source)
{
    return compile (source, "t.score");
}

Score compileOk (const std::string& source)
{
    const auto result = compileScore (source);
    INFO (result.report (source, "t.score"));
    REQUIRE (result.ok());
    return *result.score;
}

/** The two voices' pitches at each step they both sound on. */
struct Pair { int step; int lead; int alto; };

std::vector<Pair> pairsIn (const Score& score, const std::string& pattern)
{
    std::map<int, std::pair<int, int>> byStep;

    for (const auto& p : score.patterns)
    {
        if (p.name != pattern)
            continue;

        for (const auto& note : p.notes)
        {
            auto& slot = byStep[note.startStep];

            // Track 0 is `lead` and track 1 is `alto`, in declaration order.
            if (note.track == 0)
                slot.first = note.pitch;
            else
                slot.second = note.pitch;
        }
    }

    std::vector<Pair> pairs;

    for (const auto& [step, both] : byStep)
        if (both.first > 0 && both.second > 0)
            pairs.push_back ({ step, both.first, both.second });

    return pairs;
}

int within (int semitones)
{
    return ((semitones % 12) + 12) % 12;
}

int parallelsOf (const std::vector<Pair>& pairs, int interval)
{
    auto count = 0;

    for (std::size_t i = 1; i < pairs.size(); ++i)
    {
        const auto leadStep = pairs[i].lead - pairs[i - 1].lead;
        const auto altoStep = pairs[i].alto - pairs[i - 1].alto;

        const auto together = leadStep != 0 && ((leadStep > 0) == (altoStep > 0))
                           && altoStep != 0;

        if (together
            && within (pairs[i].lead - pairs[i].alto) == interval
            && within (pairs[i - 1].lead - pairs[i - 1].alto) == interval)
            ++count;
    }

    return count;
}

} // namespace

TEST_CASE ("a counterpoint voice answers one already written",
           "[score][counterpoint]")
{
    const auto score = compileOk (duet ("      parallel-fifths  forbid\n"
                                        "      parallel-octaves forbid\n"));

    const auto pairs = pairsIn (score, "duet");

    // Both voices sound, on every onset - a counterpoint that produced nothing
    // would otherwise pass every rule there is.
    INFO ("pairs: " << pairs.size());
    REQUIRE (pairs.size() == 16);
}

TEST_CASE ("forbidden parallels do not happen", "[score][counterpoint]")
{
    const auto score = compileOk (duet ("      parallel-fifths  forbid\n"
                                        "      parallel-octaves forbid\n"));

    const auto pairs = pairsIn (score, "duet");

    REQUIRE (parallelsOf (pairs, 7) == 0);
    REQUIRE (parallelsOf (pairs, 0) == 0);
}

TEST_CASE ("a forbidden voice crossing does not happen", "[score][counterpoint]")
{
    // Which side a voice sits on comes from the two declared RANGES, which is
    // the only place the answer is written down. An earlier version inferred it
    // from the first note the other voice happened to play; the two lines
    // started on the same pitch, it picked the wrong side, and then the rule
    // forbade the wrong direction and every note crossed.
    const auto score = compileOk (duet ("      voice-crossing forbid\n"));

    const auto pairs = pairsIn (score, "duet");
    REQUIRE_FALSE (pairs.empty());

    for (const auto& pair : pairs)
    {
        INFO ("step " << pair.step << ": lead " << pair.lead << ", alto " << pair.alto);
        REQUIRE (pair.alto <= pair.lead);
    }
}

TEST_CASE ("the rules actually change the line", "[score][counterpoint]")
{
    // Otherwise every rule test above passes vacuously: a search that ignored
    // its rules entirely would still produce a line without parallels most of
    // the time.
    const auto strict = pairsIn (compileOk (duet ("      leaps  forbid\n"
                                                  "      repeats forbid\n")), "duet");

    const auto loose = pairsIn (compileOk (duet ("      leaps  soft 0.1\n")), "duet");

    REQUIRE_FALSE (strict.empty());
    REQUIRE (strict.size() == loose.size());

    auto differing = 0;

    for (std::size_t i = 0; i < strict.size(); ++i)
        if (strict[i].alto != loose[i].alto)
            ++differing;

    INFO ("notes that differ: " << differing);
    REQUIRE (differing > 0);
}

TEST_CASE ("forbidding a leap keeps the line stepping", "[score][counterpoint]")
{
    const auto pairs = pairsIn (compileOk (duet ("      leaps forbid\n")), "duet");

    REQUIRE_FALSE (pairs.empty());

    for (std::size_t i = 1; i < pairs.size(); ++i)
    {
        const auto step = std::abs (pairs[i].alto - pairs[i - 1].alto);
        INFO ("step " << i << " moves " << step << " semitones");
        REQUIRE (step <= 4);
    }
}

TEST_CASE ("a counterpoint voice is the same every compile", "[score][counterpoint]")
{
    const auto rules = "      parallel-fifths forbid\n      leaps soft 1.5\n";

    const auto once = pairsIn (compileOk (duet (rules)), "duet");
    const auto again = pairsIn (compileOk (duet (rules)), "duet");

    REQUIRE_FALSE (once.empty());

    for (std::size_t i = 0; i < once.size(); ++i)
        REQUIRE (once[i].alto == again[i].alto);
}

TEST_CASE ("a part that writes nothing says so", "[score][counterpoint]")
{
    // The failure this exists to catch: `rhythmFor` did not know about a
    // counterpoint's rhythm, so an entire voice vanished and the compile
    // reported complete success.
    const auto source =
        "song {\n  tempo 96\n  meter 4/4\n  key D minor\n}\n"
        "channel lead {\n  mixer 1\n  range D4..D6\n}\n"
        "channel alto {\n  mixer 2\n  range D3..D5\n}\n"
        "rhythm walk { 1/4 }\n"
        "harmony h { i | V }\n"
        "section duet {\n  length 2 bars\n  harmony h\n"
        "  part lead {\n    melody {\n      rhythm walk\n    }\n  }\n"
        "  part alto {\n    counterpoint against lead {\n"
        "    }\n  }\n"      // no rhythm at all
        "}\n"
        "arrangement {\n  duet\n}\n";

    const auto result = compileScore (source);

    INFO (result.report (source, "t.score"));
    REQUIRE (result.ok());          // a warning, not an error

    auto warned = false;

    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.code == "W604")
            warned = true;

    REQUIRE (warned);
}

TEST_CASE ("counterpoint needs a voice that exists, and not itself",
           "[score][counterpoint]")
{
    const auto with = [] (const std::string& against)
    {
        return
            "song {\n  tempo 96\n  meter 4/4\n  key D minor\n}\n"
            "channel lead {\n  mixer 1\n  range D4..D6\n}\n"
            "rhythm walk { 1/4 }\n"
            "harmony h { i | V }\n"
            "section duet {\n  length 2 bars\n  harmony h\n"
            "  part lead {\n    counterpoint against " + against + " {\n"
            "      rhythm walk\n    }\n  }\n}\n"
            "arrangement {\n  duet\n}\n";
    };

    const auto missing = compileScore (with ("nobody"));
    REQUIRE_FALSE (missing.ok());
    REQUIRE (missing.diagnostics.front().code == "E244");

    // A voice answering itself is the one that would otherwise look plausible
    // and read its own half-written output.
    const auto itself = compileScore (with ("lead"));
    REQUIRE_FALSE (itself.ok());
    REQUIRE (itself.diagnostics.front().code == "E245");
}

TEST_CASE ("giving up a rule is reported, never silent", "[score][counterpoint]")
{
    // Every rule forbidden at once, in a range one octave wide: there is not
    // enough room to satisfy all of them, so something has to give. What must
    // NOT happen is a voice that quietly went where it was told not to, or one
    // that fell silent.
    const auto source = duet ("      range D4..D5\n"
                              "      parallel-fifths      forbid\n"
                              "      parallel-octaves     forbid\n"
                              "      direct-fifths        forbid\n"
                              "      voice-crossing       forbid\n"
                              "      dissonance-on-strong forbid\n"
                              "      leaps                forbid\n"
                              "      repeats              forbid\n");

    const auto result = compileScore (source);

    INFO (result.report (source, "t.score"));
    REQUIRE (result.ok());

    auto relaxations = 0;

    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.code == "W603")
            ++relaxations;

    INFO ("relaxations reported: " << relaxations);
    REQUIRE (relaxations > 0);

    // And it still wrote a line rather than falling silent.
    REQUIRE_FALSE (pairsIn (*result.score, "duet").empty());
}
