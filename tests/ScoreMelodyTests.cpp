// Generating a melodic line over a harmony.
//
// Split out of ScoreGeneratorTests.cpp, along the Catch2 tags it already
// carried. The fixture is ScoreGenHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <set>
#include <string>

#include "ScoreGenHarness.h"

using namespace dew::lang;
using namespace dew::testing;

TEST_CASE ("a rhythm tiles to the onsets it names", "[score][melody]")
{
    // quarter, quarter, half in 4/4 at 4 steps per beat: one bar exactly.
    const auto onsets = tileRhythm (rhythmOf ({ { 1, 4 }, { 1, 4 }, { 1, 2 } }), 32, 16, 4, 4);

    REQUIRE (onsets.size() == 6); // two bars of the three-onset cycle

    REQUIRE (onsets[0].startStep == 0);
    REQUIRE (onsets[0].lengthSteps == 4);
    REQUIRE (onsets[1].startStep == 4);
    REQUIRE (onsets[2].startStep == 8);
    REQUIRE (onsets[2].lengthSteps == 8);
    REQUIRE (onsets[3].startStep == 16); // the cycle restarts on the bar
}

TEST_CASE ("a note is never left running past its section", "[score][melody]")
{
    // A pattern is windowed by its clip, so an over-long note would retrigger
    // on the next repeat rather than ring on.
    const auto onsets = tileRhythm (rhythmOf ({ { 1, 1 } }), 20, 16, 4, 4);

    REQUIRE_FALSE (onsets.empty());

    for (const auto& onset : onsets)
    {
        REQUIRE (onset.startStep >= 0);
        REQUIRE (onset.startStep + onset.lengthSteps <= 20);
    }
}

TEST_CASE ("a tie holds the note before it rather than starting one", "[score][melody]")
{
    RhythmSpec rhythm;
    rhythm.name = "r";
    rhythm.steps.push_back ({ { 1, 4 }, false, false, {} });
    rhythm.steps.push_back ({ { 1, 4 }, false, true, {} }); // ~ 1/4

    const auto onsets = tileRhythm (rhythm, 16, 16, 4, 4, false);

    REQUIRE (onsets.size() == 2);
    REQUIRE (onsets[0].lengthSteps == 8); // 4 + 4, held
    REQUIRE (onsets[0].startStep == 0);
}

TEST_CASE ("metric strength follows the metre", "[score][melody]")
{
    // 4/4 at four steps per beat.
    REQUIRE (strengthAt (0, 16, 4, 4) == MetricStrength::barStart);
    REQUIRE (strengthAt (8, 16, 4, 4) == MetricStrength::strongBeat); // beat 3
    REQUIRE (strengthAt (4, 16, 4, 4) == MetricStrength::beat);
    REQUIRE (strengthAt (2, 16, 4, 4) == MetricStrength::offbeat);
    REQUIRE (strengthAt (1, 16, 4, 4) == MetricStrength::subdivision);

    // Weights are ordered, which the mute budget relies on.
    REQUIRE (weightOf (MetricStrength::barStart) > weightOf (MetricStrength::strongBeat));
    REQUIRE (weightOf (MetricStrength::strongBeat) > weightOf (MetricStrength::beat));
    REQUIRE (weightOf (MetricStrength::beat) > weightOf (MetricStrength::offbeat));
    REQUIRE (weightOf (MetricStrength::offbeat) > weightOf (MetricStrength::subdivision));
}

TEST_CASE ("a mute budget drops the weakest onsets and never a whole window", "[score][melody]")
{
    auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 32, 16, 4, 4);
    REQUIRE (onsets.size() == 16);

    const SeedPath path { 42 };
    applyMuteBudget (onsets, 1, 4, path, 16);

    // Exactly one per window of four, and never all of one.
    for (std::size_t start = 0; start < onsets.size(); start += 4)
    {
        auto rests = 0;

        for (auto i = start; i < start + 4; ++i)
            if (onsets[i].isRest)
                ++rests;

        INFO ("window at " << start);
        REQUIRE (rests == 1);
    }

    // And the very first onset always sounds: a line that opens with a rest
    // reads as a mistake rather than a choice.
    REQUIRE_FALSE (onsets.front().isRest);
}

TEST_CASE ("a mute budget takes the weak beats, never the strong ones", "[score][melody]")
{
    // "Weakest first" is the whole claim, and until this test nothing checked
    // WHICH onsets went quiet - only how many. A budget that silenced the
    // downbeat and kept the offbeat would have passed every test there was.
    auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 32, 16, 4, 4);
    REQUIRE (onsets.size() == 16);

    // Eighths in 4/4 alternate beat and offbeat, so a window of one bar has
    // four weak onsets and four strong ones to choose between.
    const SeedPath path { 1234 };
    applyMuteBudget (onsets, 2, 8, path, 16);

    for (const auto& onset : onsets)
    {
        INFO ("onset at step " << onset.startStep);

        if (onset.isRest)
            REQUIRE (onset.strength == MetricStrength::offbeat);
    }
}

TEST_CASE ("a mute budget draws the same notes every time it is asked", "[score][melody]")
{
    // The same seed and the same onsets, over and over inside one process.
    // This is what an inconsistent sort comparator failed: it read past the end
    // of its range, so the answer depended on what was in memory at the time.
    const SeedPath path { 99 };

    const auto muted = [&path]
    {
        auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 64, 16, 4, 4);
        applyMuteBudget (onsets, 3, 8, path, 16);

        std::vector<int> steps;

        for (const auto& onset : onsets)
            if (onset.isRest)
                steps.push_back (onset.startStep);

        return steps;
    };

    const auto expected = muted();
    REQUIRE_FALSE (expected.empty());

    for (auto i = 0; i < 32; ++i)
    {
        INFO ("attempt " << i);
        REQUIRE (muted() == expected);
    }
}

TEST_CASE ("a budget wider than its window still leaves a note", "[score][melody]")
{
    auto onsets = tileRhythm (rhythmOf ({ { 1, 4 } }), 32, 16, 4, 4);
    const auto total = onsets.size();

    const SeedPath path { 7 };
    applyMuteBudget (onsets, 100, 4, path, 16);

    auto sounding = 0;

    for (const auto& onset : onsets)
        if (! onset.isRest)
            ++sounding;

    INFO ("sounding " << sounding << " of " << total);
    REQUIRE (sounding >= (int) total / 4);
}

TEST_CASE ("a melody puts a chord tone on every strong beat when told to", "[score][melody]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), chordFrom ("V", 1) }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);
    REQUIRE_FALSE (bag.hasErrors());

    const auto onsets = tileRhythm (rhythmOf ({ { 1, 4 } }), 32, 16, 4, 4);

    MelodySpec melody;
    melody.strong = StrongRule::chordTones;
    melody.variance = 0.0f;

    const auto notes = generateMelody (onsets, spans, melody, 60, 84, SeedPath { 1 });

    REQUIRE (notes.size() == onsets.size());

    auto strongChecked = 0;

    for (const auto& note : notes)
    {
        const auto strength = strengthAt (note.startStep, 16, 4, 4);

        if (strength != MetricStrength::barStart && strength != MetricStrength::strongBeat)
            continue;

        ++strongChecked;

        const ChordSpan* span = nullptr;

        for (const auto& candidate : spans)
            if (note.startStep >= candidate.startStep && note.startStep < candidate.endStep)
                span = &candidate;

        REQUIRE (span != nullptr);
        INFO ("step " << note.startStep << " pitch " << pitchName (note.pitch));
        REQUIRE (span->chord.containsPitchClass (note.pitch));
    }

    // The control case: a test that checked no strong beats would pass too.
    REQUIRE (strongChecked >= 4);
}

TEST_CASE ("a melody stays inside its range", "[score][melody]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), chordFrom ("IV", 1),
                                                   chordFrom ("V", 1), chordFrom ("vi", 1) }),
                                      Key { 0, Mode::major }, 64, 16, 4, 4, bag);

    const auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 64, 16, 4, 4);

    MelodySpec melody;
    melody.variance = 0.5f;

    const auto notes = generateMelody (onsets, spans, melody, 65, 77, SeedPath { 3 });

    REQUIRE_FALSE (notes.empty());

    for (const auto& note : notes)
    {
        REQUIRE (note.pitch >= 65);
        REQUIRE (note.pitch <= 77);
    }
}

TEST_CASE ("variance zero is the same melody every time", "[score][melody]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), chordFrom ("V", 1) }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);
    const auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 32, 16, 4, 4);

    MelodySpec melody;
    melody.variance = 0.0f;

    const auto a = generateMelody (onsets, spans, melody, 60, 84, SeedPath { 5 });
    const auto b = generateMelody (onsets, spans, melody, 60, 84, SeedPath { 5 });

    REQUIRE (a.size() == b.size());

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i].pitch == b[i].pitch);

    // A different seed with variance 0 is still the same, because at zero the
    // generator is a pure argmin with nothing random in it at all.
    const auto c = generateMelody (onsets, spans, melody, 60, 84, SeedPath { 999 });

    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE (a[i].pitch == c[i].pitch);
}

TEST_CASE ("variance above zero explores, and still reproduces", "[score][melody]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), chordFrom ("V", 1) }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);
    const auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 32, 16, 4, 4);

    MelodySpec plain;
    plain.variance = 0.0f;

    MelodySpec varied;
    varied.variance = 0.8f;

    const auto flat = generateMelody (onsets, spans, plain, 60, 84, SeedPath { 5 });
    const auto wild = generateMelody (onsets, spans, varied, 60, 84, SeedPath { 5 });
    const auto again = generateMelody (onsets, spans, varied, 60, 84, SeedPath { 5 });

    // Reproducible...
    REQUIRE (wild.size() == again.size());

    for (std::size_t i = 0; i < wild.size(); ++i)
        REQUIRE (wild[i].pitch == again[i].pitch);

    // ...and actually different from the argmin, or the knob does nothing.
    auto differences = 0;

    for (std::size_t i = 0; i < wild.size() && i < flat.size(); ++i)
        if (wild[i].pitch != flat[i].pitch)
            ++differences;

    INFO ("differences " << differences << " of " << wild.size());
    REQUIRE (differences > 0);

    // A different seed explores differently.
    const auto other = generateMelody (onsets, spans, varied, 60, 84, SeedPath { 6 });
    auto seedDifferences = 0;

    for (std::size_t i = 0; i < wild.size() && i < other.size(); ++i)
        if (wild[i].pitch != other[i].pitch)
            ++seedDifferences;

    REQUIRE (seedDifferences > 0);
}

TEST_CASE ("a declared leap limit is a wall, not a cost", "[score][melody]")
{
    // A cost lets a bad enough alternative buy a leap anyway. "No jump wider
    // than a fourth" is a statement about the line, so it filters.
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), chordFrom ("V", 1) }),
                                      Key { 0, Mode::major }, 64, 16, 4, 4, bag);
    REQUIRE_FALSE (bag.hasErrors());

    const auto onsets = tileRhythm (rhythmOf ({ { 1, 8 } }), 64, 16, 4, 4);

    MelodySpec melody;
    melody.maxLeap = 4;
    melody.variance = 0.8f; // enough jitter to want a leap if it could

    const auto line = generateMelody (onsets, spans, melody, 48, 84, SeedPath { 5 });

    REQUIRE (line.size() > 4);

    for (std::size_t i = 1; i < line.size(); ++i)
    {
        const auto jump = std::abs (line[i].pitch - line[i - 1].pitch);
        INFO ("note " << i << " jumps " << jump);
        REQUIRE (jump <= 4);
    }
}

TEST_CASE ("a rhythm can be told to phase against the bar", "[score][melody]")
{
    // `align bar` restarts the cycle at every bar line, which is what makes a
    // written pattern land where it was written. `continuous` lets it run on -
    // a real effect, and never an accident.
    const auto rhythm = rhythmOf ({ { 1, 4 }, { 1, 4 }, { 1, 8 } });

    const auto aligned = tileRhythm (rhythm, 64, 16, 4, 4, true);
    const auto phasing = tileRhythm (rhythm, 64, 16, 4, 4, false);

    REQUIRE_FALSE (aligned.empty());
    REQUIRE_FALSE (phasing.empty());

    // Aligned, every bar starts an onset; phasing, the cycle is 10 steps in a
    // 16-step bar, so it cannot.
    for (const auto bar : { 0, 16, 32, 48 })
    {
        const auto onBarLine = std::any_of (aligned.begin(), aligned.end(),
                                            [bar] (const Onset& o) { return o.startStep == bar; });
        INFO ("bar line at step " << bar);
        REQUIRE (onBarLine);
    }

    REQUIRE (aligned.size() != phasing.size());
}
