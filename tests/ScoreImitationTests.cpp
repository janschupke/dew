#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "lang/Compile.h"

using namespace dew::lang;

namespace
{

std::string canon (const std::string& imitation, const std::string& lead = "")
{
    return "song {\n"
           "  tempo 100\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "  seed  0xCA10\n"
           "}\n"
           "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
           "channel echo {\n  mixer 2\n  range C3..C6\n}\n"
           "rhythm walk { 1/4 }\n"
           "harmony h { I | IV | V | I }\n"
           "section round {\n"
           "  length 4 bars\n"
           "  harmony h\n"
           "  part lead {\n    melody {\n      rhythm walk\n"
           + lead
           + "    }\n  }\n"
             "  part echo {\n    imitate lead {\n"
           + imitation
           + "    }\n  }\n"
             "}\n"
             "arrangement {\n  round\n}\n";
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

/** Every note of one track, as (step, pitch), in order. */
std::vector<std::pair<int, int>> trackIn (const Score& score, int track)
{
    std::vector<std::pair<int, int>> out;

    for (const auto& pattern : score.patterns)
        for (const auto& note : pattern.notes)
            if (note.track == track)
                out.push_back ({ note.startStep, note.pitch });

    return out;
}

} // namespace

TEST_CASE ("an imitation is the same line, later", "[score][imitation]")
{
    const auto score = compileOk (canon ("      delay 1 bar\n"));

    const auto lead = trackIn (score, 0);
    const auto echo = trackIn (score, 1);

    REQUIRE_FALSE (lead.empty());
    REQUIRE_FALSE (echo.empty());

    // A bar behind, note for note, at the same pitch - `transpose` defaults to
    // nothing, so this is a plain round.
    const auto stepsPerBar = score.stepsPerBar();

    for (const auto& [step, pitch] : echo)
    {
        const auto original = std::find (lead.begin(), lead.end(),
                                         std::pair<int, int> { step - stepsPerBar, pitch });

        INFO ("echo at step " << step << " pitch " << pitch);
        REQUIRE (original != lead.end());
    }
}

TEST_CASE ("what would fall past the end is dropped, not wrapped", "[score][imitation]")
{
    // A canon that wrapped would answer itself from the future.
    const auto score = compileOk (canon ("      delay 1 bar\n"));

    const auto lead = trackIn (score, 0);
    const auto echo = trackIn (score, 1);

    REQUIRE (echo.size() < lead.size());

    for (const auto& [step, pitch] : echo)
    {
        (void) pitch;
        REQUIRE (step < score.barsInSong * score.stepsPerBar());
    }
}

TEST_CASE ("a diatonic answer stays in the key", "[score][imitation]")
{
    // A chromatic fifth above a line is a line in a different mode. Diatonic is
    // the default for exactly that reason.
    const auto score = compileOk (canon ("      delay 1 bar\n"
                                         "      transpose 2\n"));

    const auto echo = trackIn (score, 1);
    REQUIRE_FALSE (echo.empty());

    // Every note is in C major.
    for (const auto& [step, pitch] : echo)
    {
        const auto degree = ((pitch % 12) + 12) % 12;
        INFO ("echo at step " << step << " pitch " << pitch);
        REQUIRE ((degree == 0 || degree == 2 || degree == 4 || degree == 5 || degree == 7
                  || degree == 9 || degree == 11));
    }
}

TEST_CASE ("a chromatic answer moves exactly", "[score][imitation]")
{
    const auto score = compileOk (canon ("      delay 1 bar\n"
                                         "      transpose 3\n"
                                         "      mode chromatic\n"));

    const auto lead = trackIn (score, 0);
    const auto echo = trackIn (score, 1);

    REQUIRE_FALSE (echo.empty());

    const auto stepsPerBar = score.stepsPerBar();

    for (const auto& [step, pitch] : echo)
    {
        const auto original = std::find_if (lead.begin(), lead.end(),
                                            [step, stepsPerBar] (const auto& note)
                                            { return note.first == step - stepsPerBar; });

        REQUIRE (original != lead.end());
        INFO ("echo " << pitch << " against lead " << original->second);
        REQUIRE (pitch == original->second + 3);
    }
}

TEST_CASE ("an imitation does not imitate itself", "[score][imitation]")
{
    // Reading the note list while appending to it would let the echo copy its
    // own output, one delay at a time, until the section filled up.
    const auto score = compileOk (canon ("      delay 1 bar\n"));

    const auto lead = trackIn (score, 0);
    const auto echo = trackIn (score, 1);

    // One echo per lead note that still fits, and not one more.
    REQUIRE (echo.size() <= lead.size());

    std::map<int, int> perStep;

    for (const auto& [step, pitch] : echo)
    {
        (void) pitch;
        ++perStep[step];
    }

    for (const auto& [step, count] : perStep)
    {
        INFO ("step " << step);
        REQUIRE (count == 1);
    }
}

TEST_CASE ("imitation needs a voice that exists, and not itself", "[score][imitation]")
{
    const auto source = "song {\n  tempo 100\n  meter 4/4\n  key C major\n}\n"
                        "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
                        "rhythm walk { 1/4 }\n"
                        "harmony h { I | V }\n"
                        "section round {\n  length 2 bars\n  harmony h\n"
                        "  part lead {\n    imitate lead {\n      delay 1 bar\n    }\n  }\n}\n"
                        "arrangement {\n  round\n}\n";

    const auto itself = compileScore (source);
    REQUIRE_FALSE (itself.ok());
    REQUIRE (itself.diagnostics.front().code == "E249");
}

TEST_CASE ("imitating a voice that has not been written says so", "[score][imitation]")
{
    // Parts are written in declaration order, so a voice copied before it is
    // written has nothing to copy - and a silent part is the failure that looks
    // like success.
    const auto source = "song {\n  tempo 100\n  meter 4/4\n  key C major\n}\n"
                        "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
                        "channel echo {\n  mixer 2\n  range C3..C6\n}\n"
                        "rhythm walk { 1/4 }\n"
                        "harmony h { I | V }\n"
                        "section round {\n  length 2 bars\n  harmony h\n"
                        "  part echo {\n    imitate lead {\n      delay 1 bar\n    }\n  }\n"
                        "  part lead {\n    melody {\n      rhythm walk\n    }\n  }\n}\n"
                        "arrangement {\n  round\n}\n";

    const auto result = compileScore (source);
    REQUIRE (result.ok());

    auto warned = false;

    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.code == "W605")
            warned = true;

    REQUIRE (warned);
}
