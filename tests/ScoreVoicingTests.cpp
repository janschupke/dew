// Laying a chord out as actual pitches.
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

TEST_CASE ("a shell voicing keeps the notes that name the chord", "[score][voicing]")
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

TEST_CASE ("an inversion written in the harmony reaches the bass", "[score][voicing]")
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
