#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <set>
#include <string>

#include "lang/Harmony.h"
#include "lang/Music.h"
#include "lang/Melody.h"
#include "lang/Voicing.h"

using namespace dew::lang;

namespace
{

ChordSpec chordFrom (std::string_view root, int weight = 0, int bars = 0)
{
    ChordSpec chord;
    chord.symbol = { root, 0, {} };

    if (weight > 0)
    {
        chord.hasWeight = true;
        chord.weight = weight;
    }

    if (bars > 0)
        chord.bars = bars;

    return chord;
}

HarmonySpec harmonyOf (std::vector<ChordSpec> chords, std::optional<Key> key = {})
{
    HarmonySpec harmony;
    harmony.name = "h";
    harmony.key = key;
    harmony.chords = std::move (chords);
    return harmony;
}

std::vector<int> lengthsOf (const std::vector<ChordSpan>& spans)
{
    std::vector<int> lengths;

    for (const auto& span : spans)
        lengths.push_back (span.endStep - span.startStep);

    return lengths;
}

RhythmSpec rhythmOf (std::vector<Duration> durations)
{
    RhythmSpec rhythm;
    rhythm.name = "r";

    for (const auto duration : durations)
        rhythm.steps.push_back ({ duration, false, false, {} });

    return rhythm;
}

} // namespace

// ==============================================================================
// Harmony layout
// ==============================================================================

TEST_CASE ("weights split exactly the space that is left", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // Two bars of 16 steps = 32 steps, shared 2:1:1.
    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 2),
                                                   chordFrom ("V", 1),
                                                   chordFrom ("vi", 1) }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (lengthsOf (spans) == std::vector<int> { 16, 8, 8 });

    // And the total is EXACT - the property largest-remainder buys.
    const auto lengths = lengthsOf (spans);
    REQUIRE (std::accumulate (lengths.begin(), lengths.end(), 0) == 32);
}

TEST_CASE ("an uneven split loses no steps and gains none", "[score][harmony]")
{
    // 16 steps between three equal chords is 5.33 each. Rounding each
    // independently loses a step; largest-remainder hands it to the earliest.
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I"), chordFrom ("IV"),
                                                   chordFrom ("V") }),
                                      Key { 0, Mode::major }, 16, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());

    const auto lengths = lengthsOf (spans);
    REQUIRE (std::accumulate (lengths.begin(), lengths.end(), 0) == 16);
    REQUIRE (lengths == std::vector<int> { 6, 5, 5 });
}

TEST_CASE ("absolute lengths are taken out before the weights share",
           "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // 4 bars = 64 steps. `I` takes 2 bars outright; the rest share 32.
    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 0, 2),
                                                   chordFrom ("IV", 1),
                                                   chordFrom ("V", 1) }),
                                      Key { 0, Mode::major }, 64, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (lengthsOf (spans) == std::vector<int> { 32, 16, 16 });
}

TEST_CASE ("a harmony longer than its section names the chord that overran",
           "[score][harmony]")
{
    const std::string source = "I IV V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 0, 1), chordFrom ("IV", 0, 1),
                                           chordFrom ("V", 0, 1) };
    chords[0].range = { 0, 1 };
    chords[1].range = { 2, 4 };
    chords[2].range = { 5, 6 };

    // Three bars of chords in a two-bar section.
    const auto spans = layOutHarmony (harmonyOf (chords), Key { 0, Mode::major },
                                      32, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());

    const auto& d = bag.all().front();
    REQUIRE (d.code == "E301");

    // The THIRD chord is where it crossed, not the block as a whole.
    REQUIRE (d.primary == SourceRange { 5, 6 });
}

TEST_CASE ("a harmony that leaves a gap is an error, not silent padding",
           "[score][harmony]")
{
    // Trailing silence nobody asked for is the most expensive kind of bug in
    // generated music, because it sounds plausible.
    const std::string source = "x";
    DiagnosticBag bag { source };

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 0, 1) }),
                                      Key { 0, Mode::major }, 64, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());
    REQUIRE (bag.all().front().code == "E302");
}

TEST_CASE ("a chord that would round to nothing is refused", "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    // Four steps between five chords: one of them gets zero and would simply
    // vanish from the arrangement.
    std::vector<ChordSpec> chords;

    for (int i = 0; i < 5; ++i)
        chords.push_back (chordFrom ("I"));

    const auto spans = layOutHarmony (harmonyOf (chords), Key { 0, Mode::major },
                                      4, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());
    REQUIRE (spans.empty());
    REQUIRE (bag.all().front().code == "E303");
}

TEST_CASE ("a bar check that is not on a bar line says where it landed",
           "[score][harmony]")
{
    // The highest-value error catcher in the harmony syntax: it turns "the
    // section length changed and everything shifted" into one message.
    const std::string source = "I V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 1), chordFrom ("V", 1) };
    chords[0].range = { 0, 1 };
    chords[0].barCheckAfter = true;     // asserts a bar line after the first chord
    chords[1].range = { 2, 3 };

    // Three bars shared 1:1 puts the boundary in the middle of bar two.
    layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 48, 16, 4, 4, bag);

    REQUIRE (bag.hasErrors());

    const auto& d = bag.all().front();
    REQUIRE (d.code == "E304");
    REQUIRE (d.notes.front().find ("bar 2") != std::string::npos);
}

TEST_CASE ("a bar check that holds says nothing", "[score][harmony]")
{
    const std::string source = "I V";
    DiagnosticBag bag { source };

    auto chords = std::vector<ChordSpec> { chordFrom ("I", 1), chordFrom ("V", 1) };
    chords[0].barCheckAfter = true;

    layOutHarmony (harmonyOf (chords), Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
}

TEST_CASE ("a tonicised span carries the key its melody should use",
           "[score][harmony]")
{
    const std::string source = "x";
    DiagnosticBag bag { source };

    ChordSpec secondary;
    secondary.symbol = { "V7", 0, "vi" };
    secondary.hasWeight = true;
    secondary.weight = 1;

    const auto spans = layOutHarmony (harmonyOf ({ chordFrom ("I", 1), secondary }),
                                      Key { 0, Mode::major }, 32, 16, 4, 4, bag);

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (spans.size() == 2);

    REQUIRE (spans[0].localKey.tonicPc == 0);
    REQUIRE (spans[1].localKey.tonicPc == 9);       // A, tonicised by V7/vi
    REQUIRE (spans[1].localKey.mode == Mode::harmonicMinor);
}

// ==============================================================================
// Voicing
// ==============================================================================

TEST_CASE ("a voicing puts the right notes in the register", "[score][voicing]")
{
    VoicingSpec spec;
    spec.voices = 3;
    spec.lowPitch = 48;
    spec.highPitch = 72;

    const auto cMajor = *resolveChord ({ "I", 0, {} }, Key { 0, Mode::major }, nullptr);
    const auto pitches = voiceChord (cMajor.chord, spec, {});

    REQUIRE (pitches.size() == 3);

    // Ascending, in range, and every note a chord tone.
    for (std::size_t i = 1; i < pitches.size(); ++i)
        REQUIRE (pitches[i] > pitches[i - 1]);

    for (const auto pitch : pitches)
    {
        REQUIRE (pitch >= spec.lowPitch - 12);
        REQUIRE (pitch <= spec.highPitch + 12);
        REQUIRE (cMajor.chord.containsPitchClass (pitch));
    }
}

TEST_CASE ("smooth leading really does move the voices less", "[score][voicing]")
{
    // The claim the cost function exists to make. Compared against the same
    // progression voiced with motion ignored, over a real chord sequence.
    const Key cMajor { 0, Mode::major };

    VoicingSpec smooth;
    smooth.voices = 4;
    smooth.lowPitch = 48;
    smooth.highPitch = 76;
    smooth.motion = Motion::smooth;

    auto fixedSpec = smooth;
    fixedSpec.motion = Motion::fixed;

    const char* progression[] = { "I", "vi", "IV", "V", "I" };

    auto totalFor = [&] (const VoicingSpec& spec)
    {
        std::vector<int> previous;
        auto total = 0;

        for (const auto* symbol : progression)
        {
            const auto chord = resolveChord ({ symbol, 0, {} }, cMajor, nullptr);
            REQUIRE (chord.has_value());

            const auto pitches = voiceChord (chord->chord, spec, previous);

            if (! previous.empty())
                total += voiceLeadingCost (previous, pitches);

            previous = pitches;
        }

        return total;
    };

    const auto smoothTotal = totalFor (smooth);
    const auto fixedTotal = totalFor (fixedSpec);

    INFO ("smooth " << smoothTotal << "  fixed " << fixedTotal);
    REQUIRE (smoothTotal < fixedTotal);
}

TEST_CASE ("voicing the same chord twice gives the same notes", "[score][voicing]")
{
    // No hidden state and no rng: ties break to the first candidate in a fixed
    // enumeration order, so this is a pure function.
    VoicingSpec spec;
    spec.voices = 4;

    const auto chord = resolveChord ({ "V7", 0, {} }, Key { 0, Mode::major }, nullptr);
    const std::vector<int> previous { 60, 64, 67, 72 };

    REQUIRE (voiceChord (chord->chord, spec, previous)
             == voiceChord (chord->chord, spec, previous));
}

TEST_CASE ("drop2 lowers the second voice from the top", "[score][voicing]")
{
    VoicingSpec close;
    close.voices = 4;
    close.lowPitch = 48;
    close.highPitch = 84;
    close.spread = Spread::close;
    close.motion = Motion::fixed;

    auto dropped = close;
    dropped.spread = Spread::drop2;

    const auto chord = resolveChord ({ "Imaj7", 0, {} }, Key { 0, Mode::major }, nullptr);

    const auto closeVoicing = voiceChord (chord->chord, close, {});
    const auto droppedVoicing = voiceChord (chord->chord, dropped, {});

    REQUIRE (closeVoicing.size() == 4);
    REQUIRE (droppedVoicing.size() == 4);

    // A dropped voicing spans wider than a close one of the same chord.
    const auto closeSpan = closeVoicing.back() - closeVoicing.front();
    const auto droppedSpan = droppedVoicing.back() - droppedVoicing.front();

    INFO ("close span " << closeSpan << "  drop2 span " << droppedSpan);
    REQUIRE (droppedSpan > closeSpan);
}

TEST_CASE ("a shell voicing keeps the notes that name the chord",
           "[score][voicing]")
{
    VoicingSpec spec;
    spec.voices = 3;
    spec.spread = Spread::shell;

    const auto chord = resolveChord ({ "V7", 0, {} }, Key { 0, Mode::major }, nullptr);
    const auto pitches = voiceChord (chord->chord, spec, {});

    std::set<int> classes;

    for (const auto pitch : pitches)
        classes.insert (pitchClassOf (pitch));

    // Root, third and seventh of G7: G, B, F. The fifth is what a shell drops.
    REQUIRE (classes.count (7) == 1);
    REQUIRE (classes.count (11) == 1);
    REQUIRE (classes.count (5) == 1);
}

// ==============================================================================
// Rhythm and melody
// ==============================================================================

TEST_CASE ("a rhythm tiles to the onsets it names", "[score][melody]")
{
    // quarter, quarter, half in 4/4 at 4 steps per beat: one bar exactly.
    const auto onsets = tileRhythm (rhythmOf ({ { 1, 4 }, { 1, 4 }, { 1, 2 } }),
                                    32, 16, 4, 4);

    REQUIRE (onsets.size() == 6);           // two bars of the three-onset cycle

    REQUIRE (onsets[0].startStep == 0);
    REQUIRE (onsets[0].lengthSteps == 4);
    REQUIRE (onsets[1].startStep == 4);
    REQUIRE (onsets[2].startStep == 8);
    REQUIRE (onsets[2].lengthSteps == 8);
    REQUIRE (onsets[3].startStep == 16);    // the cycle restarts on the bar
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

TEST_CASE ("a tie holds the note before it rather than starting one",
           "[score][melody]")
{
    RhythmSpec rhythm;
    rhythm.name = "r";
    rhythm.steps.push_back ({ { 1, 4 }, false, false, {} });
    rhythm.steps.push_back ({ { 1, 4 }, false, true, {} });    // ~ 1/4

    const auto onsets = tileRhythm (rhythm, 16, 16, 4, 4, false);

    REQUIRE (onsets.size() == 2);
    REQUIRE (onsets[0].lengthSteps == 8);     // 4 + 4, held
    REQUIRE (onsets[0].startStep == 0);
}

TEST_CASE ("metric strength follows the metre", "[score][melody]")
{
    // 4/4 at four steps per beat.
    REQUIRE (strengthAt (0, 16, 4, 4) == MetricStrength::barStart);
    REQUIRE (strengthAt (8, 16, 4, 4) == MetricStrength::strongBeat);   // beat 3
    REQUIRE (strengthAt (4, 16, 4, 4) == MetricStrength::beat);
    REQUIRE (strengthAt (2, 16, 4, 4) == MetricStrength::offbeat);
    REQUIRE (strengthAt (1, 16, 4, 4) == MetricStrength::subdivision);

    // Weights are ordered, which the mute budget relies on.
    REQUIRE (weightOf (MetricStrength::barStart) > weightOf (MetricStrength::strongBeat));
    REQUIRE (weightOf (MetricStrength::strongBeat) > weightOf (MetricStrength::beat));
    REQUIRE (weightOf (MetricStrength::beat) > weightOf (MetricStrength::offbeat));
    REQUIRE (weightOf (MetricStrength::offbeat) > weightOf (MetricStrength::subdivision));
}

TEST_CASE ("a mute budget drops the weakest onsets and never a whole window",
           "[score][melody]")
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

TEST_CASE ("a mute budget takes the weak beats, never the strong ones",
           "[score][melody]")
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

TEST_CASE ("a mute budget draws the same notes every time it is asked",
           "[score][melody]")
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

TEST_CASE ("a melody puts a chord tone on every strong beat when told to",
           "[score][melody]")
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

TEST_CASE ("an inversion written in the harmony reaches the bass",
           "[score][voicing]")
{
    // `i^1` parsed, resolved and then changed nothing that could be heard: the
    // voicer enumerated every inversion and chose on cost alone. Writing an
    // inversion has to move the bass or the notation is decoration.
    const Key cMajor { 0, Mode::major };

    const auto voice = [&cMajor] (int inversion, BassRule bass)
    {
        const auto resolved = resolveChord ({ "I", inversion, {} }, cMajor);
        REQUIRE (resolved.has_value());

        VoicingSpec spec;
        spec.voices = 3;
        spec.lowPitch = 48;
        spec.highPitch = 72;
        spec.bass = bass;

        return voiceChord (resolved->chord, spec, {});
    };

    const auto pitchClassOfBass = [] (const std::vector<int>& pitches)
    {
        REQUIRE_FALSE (pitches.empty());
        return ((pitches.front() % 12) + 12) % 12;
    };

    // C major in first inversion puts E at the bottom - pitch class 4.
    REQUIRE (pitchClassOfBass (voice (1, BassRule::fromInversion)) == 4);

    // The second inversion puts G there.
    REQUIRE (pitchClassOfBass (voice (2, BassRule::fromInversion)) == 7);

    // `root` overrides what was written, and `any` ignores it - which is what
    // the voicer did for every chord before this rule existed.
    REQUIRE (pitchClassOfBass (voice (1, BassRule::root)) == 0);
    REQUIRE_FALSE (voice (1, BassRule::any).empty());

    // And a chord with NO inversion mark still leaves the voicer free, which is
    // what lets it find a smooth bass line.
    REQUIRE_FALSE (voice (0, BassRule::fromInversion).empty());
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
    melody.variance = 0.8f;      // enough jitter to want a leap if it could

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
                                            [bar] (const Onset& o)
                                            { return o.startStep == bar; });
        INFO ("bar line at step " << bar);
        REQUIRE (onBarLine);
    }

    REQUIRE (aligned.size() != phasing.size());
}
