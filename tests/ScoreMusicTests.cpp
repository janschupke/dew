#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lang/Music.h"

using namespace dew::lang;

namespace
{

Chord chordOf (std::string_view symbol, const Key& key, int inversion = 0, std::string_view of = {})
{
    std::string reason;
    const auto resolved = resolveChord ({ symbol, inversion, of }, key, &reason);
    INFO ("resolving " << symbol << ": " << reason);
    REQUIRE (resolved.has_value());
    return resolved->chord;
}

} // namespace

TEST_CASE ("middle C is 60 and is called C4, as the piano roll says", "[score][music]")
{
    // Not a free choice: PianoRollComponent::noteName spells a pitch as
    // `pitch / 12 - 1`. Any other convention would put a score's notes an octave
    // from where its author read them in the roll.
    REQUIRE (parsePitch ("C4") == 60);
    REQUIRE (pitchName (60) == "C4");

    REQUIRE (parsePitch ("C-1") == 0);
    REQUIRE (parsePitch ("A4") == 69); // concert A
    REQUIRE (parsePitch ("F#3") == 54);
    REQUIRE (parsePitch ("Bb2") == 46);
    REQUIRE (parsePitch ("C3") == 48);
    REQUIRE (parsePitch ("C5") == 72);

    REQUIRE (pitchName (69) == "A4");
    REQUIRE (pitchName (54) == "F#3");

    // Enharmonics land on the same pitch even though the name comes back sharp.
    REQUIRE (parsePitch ("Bb2") == parsePitch ("A#2"));
}

TEST_CASE ("what is not a pitch is refused", "[score][music]")
{
    for (const auto* text : { "", "H4", "C", "4", "C#", "Cx4", "C4x", "C99", "c" })
    {
        INFO ("text " << text);
        REQUIRE_FALSE (parsePitch (text).has_value());
    }
}

TEST_CASE ("a key needs a tonic and a mode", "[score][music]")
{
    const auto fMinor = parseKey ("F", "minor");
    REQUIRE (fMinor.has_value());
    REQUIRE (fMinor->tonicPc == 5);
    REQUIRE (fMinor->mode == Mode::minor);

    REQUIRE (parseKey ("Bb", "major").has_value());
    REQUIRE (parseKey ("C", "dorian").has_value());

    REQUIRE_FALSE (parseKey ("H", "major").has_value());
    REQUIRE_FALSE (parseKey ("C", "wobbly").has_value());
    REQUIRE_FALSE (parseKey ("C4", "major").has_value()); // a key has no octave
}

TEST_CASE ("scale membership follows the mode", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    REQUIRE (isScaleTone (cMajor, 60));       // C
    REQUIRE (isScaleTone (cMajor, 62));       // D
    REQUIRE_FALSE (isScaleTone (cMajor, 61)); // C#

    const Key cMinor { 0, Mode::minor };
    REQUIRE (isScaleTone (cMinor, 63));       // Eb
    REQUIRE_FALSE (isScaleTone (cMinor, 64)); // E

    // An octave of C major has eight scale tones counting both Cs.
    const auto octave = scalePitchesBetween (cMajor, 60, 72);
    REQUIRE (octave.size() == 8);
    REQUIRE (octave.front() == 60);
    REQUIRE (octave.back() == 72);
}

TEST_CASE ("a roman numeral names a degree of its key", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    // Case carries quality: I is major, ii is minor.
    REQUIRE (chordOf ("I", cMajor).pitchClasses() == std::vector<int> { 0, 4, 7 });
    REQUIRE (chordOf ("ii", cMajor).pitchClasses() == std::vector<int> { 2, 5, 9 });
    REQUIRE (chordOf ("V", cMajor).pitchClasses() == std::vector<int> { 7, 11, 2 });
    REQUIRE (chordOf ("vi", cMajor).pitchClasses() == std::vector<int> { 9, 0, 4 });

    // The same numerals in a minor key land on the minor scale's degrees.
    const Key aMinor { 9, Mode::minor };
    REQUIRE (chordOf ("i", aMinor).pitchClasses() == std::vector<int> { 9, 0, 4 });
    REQUIRE (chordOf ("VI", aMinor).pitchClasses() == std::vector<int> { 5, 9, 0 });
    REQUIRE (chordOf ("VII", aMinor).pitchClasses() == std::vector<int> { 7, 11, 2 });
}

TEST_CASE ("an accidental prefix borrows a chord from outside the key", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    // bVII in C major is a major triad on Bb - the borrowed chord, spelled with
    // no extra syntax beyond the prefix.
    REQUIRE (chordOf ("bVII", cMajor).rootPc == 10);
    REQUIRE (chordOf ("bVII", cMajor).pitchClasses() == std::vector<int> { 10, 2, 5 });

    REQUIRE (chordOf ("bVI", cMajor).rootPc == 8);
    REQUIRE (chordOf ("#iv", cMajor).rootPc == 6);
}

TEST_CASE ("qualities and extensions build the chords they name", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    REQUIRE (chordOf ("V7", cMajor).pitchClasses() == std::vector<int> { 7, 11, 2, 5 });
    REQUIRE (chordOf ("Imaj7", cMajor).pitchClasses() == std::vector<int> { 0, 4, 7, 11 });
    REQUIRE (chordOf ("ii7", cMajor).pitchClasses() == std::vector<int> { 2, 5, 9, 0 });
    REQUIRE (chordOf ("viidim7", cMajor).pitchClasses() == std::vector<int> { 11, 2, 5, 8 });
    REQUIRE (chordOf ("iim7b5", cMajor).pitchClasses() == std::vector<int> { 2, 5, 8, 0 });
    REQUIRE (chordOf ("Isus4", cMajor).pitchClasses() == std::vector<int> { 0, 5, 7 });
    REQUIRE (chordOf ("V9", cMajor).pitchClasses() == std::vector<int> { 7, 11, 2, 5, 9 });

    // A ninth keeps its interval rather than collapsing to a second: dropping
    // the fifth of a ninth chord and dropping its second are different chords.
    const auto ninth = chordOf ("V9", cMajor);
    REQUIRE (ninth.intervals == std::vector<int> { 0, 4, 7, 10, 14 });
}

TEST_CASE ("an absolute chord is told apart from a numeral with no lookahead", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    // Uppercase A-G is absolute; a leading b, # or roman letter is a numeral.
    // I and V are not note letters, and A-G are not roman letters, so the two
    // sets never collide.
    REQUIRE (chordOf ("Cm7", cMajor).pitchClasses() == std::vector<int> { 0, 3, 7, 10 });
    REQUIRE (chordOf ("Ab", cMajor).pitchClasses() == std::vector<int> { 8, 0, 3 });
    REQUIRE (chordOf ("F#dim7", cMajor).pitchClasses() == std::vector<int> { 6, 9, 0, 3 });
    REQUIRE (chordOf ("Bb9", cMajor).rootPc == 10);

    // "Bb" is the note B flat; "bVII" is the flattened seventh degree. In C
    // major they happen to be the same chord, which is exactly why the rule has
    // to be lexical rather than a guess.
    REQUIRE (chordOf ("Bb", cMajor).rootPc == chordOf ("bVII", cMajor).rootPc);

    // "Cmaj7" is not "C minor aj7".
    REQUIRE (chordOf ("Cmaj7", cMajor).pitchClasses() == std::vector<int> { 0, 4, 7, 11 });
    REQUIRE (chordOf ("Cm", cMajor).pitchClasses() == std::vector<int> { 0, 3, 7 });
}

TEST_CASE ("an inversion moves the bass, not the chord", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    const auto root = chordOf ("I", cMajor, 0);
    const auto first = chordOf ("I", cMajor, 1);
    const auto second = chordOf ("I", cMajor, 2);

    REQUIRE (root.bassPc == 0);
    REQUIRE (first.bassPc == 4);
    REQUIRE (second.bassPc == 7);

    // Same notes throughout - an inversion is which one is lowest.
    REQUIRE (root.pitchClasses() == first.pitchClasses());
    REQUIRE (root.pitchClasses() == second.pitchClasses());
}

TEST_CASE ("an inversion a chord does not have is refused", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    std::string reason;
    REQUIRE_FALSE (resolveChord ({ "I", 3, {} }, cMajor, &reason).has_value());
    INFO (reason);
    REQUIRE (reason.find ("no inversion 3") != std::string::npos);

    // A seventh chord does have a third inversion.
    REQUIRE (resolveChord ({ "V7", 3, {} }, cMajor, nullptr).has_value());
}

TEST_CASE ("a secondary dominant is read in the key it tonicises", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    // V/V in C major is D major - the dominant of G, not the dominant of C.
    const auto vOfV = chordOf ("V", cMajor, 0, "V");
    REQUIRE (vOfV.rootPc == 2);
    REQUIRE (vOfV.pitchClasses() == std::vector<int> { 2, 6, 9 });

    // V7/vi is E7, tonicising A minor.
    const auto vOfVi = chordOf ("V7", cMajor, 0, "vi");
    REQUIRE (vOfVi.rootPc == 4);
    REQUIRE (vOfVi.pitchClasses() == std::vector<int> { 4, 8, 11, 2 });
}

TEST_CASE ("a tonicisation moves the melody's key too", "[score][music]")
{
    // The one field that makes a secondary dominant sound like one: without it
    // a melody keeps drawing scale tones from the home key and fights the chord.
    const Key cMajor { 0, Mode::major };

    const auto plain = resolveChord ({ "V", 0, {} }, cMajor, nullptr);
    REQUIRE (plain.has_value());
    REQUIRE (plain->localKey.tonicPc == 0); // unchanged

    const auto secondary = resolveChord ({ "V7", 0, "vi" }, cMajor, nullptr);
    REQUIRE (secondary.has_value());
    REQUIRE (secondary->localKey.tonicPc == 9); // A

    // HARMONIC minor, deliberately: natural minor has no leading tone, and the
    // leading tone is the entire reason a dominant tonicises anything.
    REQUIRE (secondary->localKey.mode == Mode::harmonicMinor);

    // G# is not in C major, and not in A natural minor either - but it IS the
    // chord's own third and the leading tone of A, so the local key must
    // contain it or the melody fights the chord.
    REQUIRE_FALSE (isScaleTone (cMajor, 68));
    REQUIRE_FALSE (isScaleTone (Key { 9, Mode::minor }, 68));
    REQUIRE (isScaleTone (secondary->localKey, 68));
    REQUIRE (secondary->chord.containsPitchClass (8));
}

TEST_CASE ("a numeral needs a mode with seven degrees", "[score][music]")
{
    // A roman numeral names a scale DEGREE. A pentatonic has five, and silently
    // indexing past the end of the table would be a wrong chord rather than an
    // error.
    const Key pentatonic { 0, Mode::majorPentatonic };

    std::string reason;
    REQUIRE_FALSE (resolveChord ({ "V", 0, {} }, pentatonic, &reason).has_value());
    INFO (reason);
    REQUIRE (reason.find ("seven") != std::string::npos);

    // An absolute chord is fine there - it names no degree.
    REQUIRE (resolveChord ({ "G", 0, {} }, pentatonic, nullptr).has_value());
}

TEST_CASE ("an unknown quality is named rather than ignored", "[score][music]")
{
    const Key cMajor { 0, Mode::major };

    std::string reason;
    REQUIRE_FALSE (resolveChord ({ "Vsplendid", 0, {} }, cMajor, &reason).has_value());
    INFO (reason);
    REQUIRE (reason.find ("splendid") != std::string::npos);

    REQUIRE_FALSE (resolveChord ({ "", 0, {} }, cMajor, nullptr).has_value());
    REQUIRE_FALSE (resolveChord ({ "wobble", 0, {} }, cMajor, nullptr).has_value());
}

TEST_CASE ("every seven-note mode supports numerals and the others do not", "[score][music]")
{
    for (const auto mode :
         { Mode::major, Mode::minor, Mode::dorian, Mode::phrygian, Mode::lydian, Mode::mixolydian,
           Mode::locrian, Mode::harmonicMinor, Mode::melodicMinor })
    {
        INFO ("mode " << nameOf (mode));
        REQUIRE (degreesOf (mode).size() == 7);
        REQUIRE (supportsRomanNumerals (mode));
    }

    for (const auto mode :
         { Mode::majorPentatonic, Mode::minorPentatonic, Mode::blues, Mode::chromatic })
    {
        INFO ("mode " << nameOf (mode));
        REQUIRE_FALSE (supportsRomanNumerals (mode));
    }

    // Every mode's degrees are ascending and inside one octave, which is what
    // scalePitchesBetween assumes.
    for (const auto mode :
         { Mode::major, Mode::minor, Mode::dorian, Mode::phrygian, Mode::lydian, Mode::mixolydian,
           Mode::locrian, Mode::harmonicMinor, Mode::melodicMinor, Mode::majorPentatonic,
           Mode::minorPentatonic, Mode::blues, Mode::chromatic })
    {
        INFO ("mode " << nameOf (mode));
        const auto& degrees = degreesOf (mode);

        REQUIRE (degrees.front() == 0);

        for (std::size_t i = 1; i < degrees.size(); ++i)
            REQUIRE (degrees[i] > degrees[i - 1]);

        REQUIRE (degrees.back() < 12);
    }
}

TEST_CASE ("an accidental on a numeral is read against the major scale", "[score][music]")
{
    // The rule, and the bug it fixes. Flattening the MODE's own degree is right
    // in major and wrong in minor: the sixth of natural minor is already flat,
    // so `bVI` flattened again lands a semitone below the chord everybody
    // writing `i bVI bVII` means. Every minor-key score in this repo rendered a
    // semitone low and sounded plausible enough that nobody heard it.
    const Key aMinor { 9, Mode::minor };

    // A minor: bVI is F, bVII is G, bIII is C.
    REQUIRE (chordOf ("bVI", aMinor).rootPc == 5);
    REQUIRE (chordOf ("bVII", aMinor).rootPc == 7);
    REQUIRE (chordOf ("bIII", aMinor).rootPc == 0);

    // A bare numeral is still relative to the MODE, so the minor scale's own
    // degrees are unchanged.
    REQUIRE (chordOf ("i", aMinor).rootPc == 9);
    REQUIRE (chordOf ("iv", aMinor).rootPc == 2);
    REQUIRE (chordOf ("v", aMinor).rootPc == 4);

    // `VI` and `bVI` naming the same chord in minor is correct rather than a
    // collision: both spellings are written and both mean F.
    REQUIRE (chordOf ("VI", aMinor).rootPc == chordOf ("bVI", aMinor).rootPc);

    // And major is unchanged, which is what the rule has to preserve.
    const Key cMajor { 0, Mode::major };

    REQUIRE (chordOf ("bVII", cMajor).rootPc == 10);
    REQUIRE (chordOf ("bVI", cMajor).rootPc == 8);
    REQUIRE (chordOf ("bIII", cMajor).rootPc == 3);
    REQUIRE (chordOf ("#iv", cMajor).rootPc == 6);
}
