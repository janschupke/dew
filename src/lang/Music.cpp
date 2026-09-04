#include "lang/Music.h"

#include "lang/MusicSpelling.h"

#include <algorithm>

#include "lang/ScanCore.h"

namespace dew::lang
{

namespace
{

// --- interval tables ----------------------------------------------------------

const std::vector<int>& intervalsForSuffix (std::string_view suffix, bool minorBase,
                                            bool* recognised)
{
    static const std::vector<int> majorTriad { 0, 4, 7 };
    static const std::vector<int> minorTriad { 0, 3, 7 };
    static const std::vector<int> dimTriad { 0, 3, 6 };
    static const std::vector<int> augTriad { 0, 4, 8 };
    static const std::vector<int> sus2 { 0, 2, 7 };
    static const std::vector<int> sus4 { 0, 5, 7 };
    static const std::vector<int> dom7 { 0, 4, 7, 10 };
    static const std::vector<int> maj7 { 0, 4, 7, 11 };
    static const std::vector<int> min7 { 0, 3, 7, 10 };
    static const std::vector<int> minMaj7 { 0, 3, 7, 11 };
    static const std::vector<int> dim7 { 0, 3, 6, 9 };
    static const std::vector<int> halfDim7 { 0, 3, 6, 10 };
    static const std::vector<int> aug7 { 0, 4, 8, 10 };
    static const std::vector<int> six { 0, 4, 7, 9 };
    static const std::vector<int> minSix { 0, 3, 7, 9 };
    static const std::vector<int> add9 { 0, 4, 7, 14 };
    static const std::vector<int> dom9 { 0, 4, 7, 10, 14 };
    static const std::vector<int> maj9 { 0, 4, 7, 11, 14 };
    static const std::vector<int> min9 { 0, 3, 7, 10, 14 };
    static const std::vector<int> dom11 { 0, 4, 7, 10, 14, 17 };
    static const std::vector<int> dom13 { 0, 4, 7, 10, 14, 17, 21 };
    static const std::vector<int> sevenSus4 { 0, 5, 7, 10 };

    *recognised = true;

    if (suffix.empty())
        return minorBase ? minorTriad : majorTriad;

    // Quality words first, so "dim7" is not read as "dim" with a stray 7.
    if (suffix == "dim7" || suffix == "o7")
        return dim7;
    if (suffix == "dim" || suffix == "o")
        return dimTriad;
    if (suffix == "m7b5" || suffix == "hdim")
        return halfDim7;
    if (suffix == "aug7" || suffix == "+7")
        return aug7;
    if (suffix == "aug" || suffix == "+")
        return augTriad;
    if (suffix == "maj7" || suffix == "M7")
        return minorBase ? minMaj7 : maj7;
    if (suffix == "maj9" || suffix == "M9")
        return maj9;
    if (suffix == "sus2")
        return sus2;
    if (suffix == "sus4" || suffix == "sus")
        return sus4;
    if (suffix == "7sus4")
        return sevenSus4;
    if (suffix == "add9")
        return add9;
    if (suffix == "6")
        return minorBase ? minSix : six;
    if (suffix == "m6")
        return minSix;
    if (suffix == "m7")
        return min7;
    if (suffix == "m9")
        return min9;
    if (suffix == "7")
        return minorBase ? min7 : dom7;
    if (suffix == "9")
        return minorBase ? min9 : dom9;
    if (suffix == "11")
        return dom11;
    if (suffix == "13")
        return dom13;

    *recognised = false;
    return minorBase ? minorTriad : majorTriad;
}

/** A roman numeral's degree index, 0..6, and whether it was upper case. */
struct Numeral
{
    int degree = 0;
    bool upper = false;
    std::size_t length = 0;
};

std::optional<Numeral> readNumeral (std::string_view text, std::size_t from) noexcept
{
    // Longest first, so "iii" is not read as "ii" with a trailing "i" and "vii"
    // is not read as "v".
    struct Entry
    {
        const char* text;
        int degree;
    };

    static const Entry entries[] = {
        { "vii", 6 }, { "iii", 2 }, { "vi", 5 }, { "iv", 3 }, { "ii", 1 }, { "v", 4 }, { "i", 0 },
    };

    for (const auto& entry : entries)
    {
        const std::string_view lower { entry.text };

        if (text.size() - from < lower.size())
            continue;

        const auto candidate = text.substr (from, lower.size());

        auto matchesLower = true;
        auto matchesUpper = true;

        for (std::size_t i = 0; i < lower.size(); ++i)
        {
            if (candidate[i] != lower[i])
                matchesLower = false;

            if (candidate[i] != (char) (lower[i] - 'a' + 'A'))
                matchesUpper = false;
        }

        if (matchesLower || matchesUpper)
            return Numeral { entry.degree, matchesUpper, lower.size() };
    }

    return std::nullopt;
}

/** Everything a chord root resolves to, before inversion is applied. */
struct RootAndShape
{
    int rootPc = 0;
    std::vector<int> intervals;
    bool minorBase = false;
    std::string label;
};

std::optional<RootAndShape> resolveRoot (std::string_view text, const Key& key,
                                         std::string* failureReason)
{
    auto fail = [failureReason] (std::string reason) -> std::optional<RootAndShape>
    {
        if (failureReason != nullptr)
            *failureReason = std::move (reason);

        return std::nullopt;
    };

    if (text.empty())
        return fail ("an empty chord");

    std::size_t i = 0;

    // An accidental PREFIX means a roman numeral (`bVII`); an absolute chord
    // carries its accidental after the letter (`Bb`). That is what keeps the two
    // apart with no lookahead, and it works because I and V are not note letters
    // and A-G are not roman letters.
    const auto prefix = spelling::readAccidentals (text, i);
    const auto hadPrefix = i > 0;

    if (const auto numeral = readNumeral (text, i))
    {
        if (! supportsRomanNumerals (key.mode))
            return fail (std::string ("`") + std::string (text) + "` names a scale degree, which "
                         + nameOf (key.mode) + " does not have seven of");

        // AN ACCIDENTAL IS RELATIVE TO THE MAJOR SCALE. A bare numeral is
        // relative to the mode.
        //
        // Both halves are needed and neither alone is right. Flattening the
        // mode's own degree gives `bVII` = Bb in C major, which is correct, and
        // `bVI` = E in A MINOR, which is a semitone below what anybody writing
        // `i bVI bVII` means - the sixth of natural minor is already flat, so
        // flattening it again lands on the wrong chord. Every minor-key score
        // in this repo, `examples/amber.score` included, was rendering a
        // semitone low and sounding plausible enough that nobody heard it.
        //
        // Read against major, `bVI` is F in A minor and Ab in C major, which is
        // what the numeral means in both. A bare `VI` in A minor is F too, and
        // that they coincide is correct rather than a collision: both spellings
        // name the same chord and both are written.
        const auto& degrees = prefix != 0 ? degreesOf (Mode::major) : degreesOf (key.mode);

        const auto rootPc = pitchClassOf (key.tonicPc + degrees[(std::size_t) numeral->degree]
                                          + prefix);

        const auto suffix = text.substr (i + numeral->length);

        auto recognised = false;
        const auto& intervals = intervalsForSuffix (suffix, ! numeral->upper, &recognised);

        if (! recognised)
            return fail (std::string ("`") + std::string (suffix) + "` is not a chord quality");

        RootAndShape out;
        out.rootPc = rootPc;
        out.intervals = intervals;
        out.minorBase = ! numeral->upper;
        out.label = spelling::describePitchClass (rootPc, prefix < 0 || key.mode == Mode::minor)
                    + std::string (suffix.empty() && ! numeral->upper ? "m" : "")
                    + std::string (suffix);

        return out;
    }

    if (hadPrefix)
        return fail (std::string ("`") + std::string (text) + "` is not a chord");

    // Absolute: a letter, then any accidentals, then a quality.
    const auto letterPc = spelling::letterToPitchClass (text[0]);

    if (! letterPc.has_value() || ! (text[0] >= 'A' && text[0] <= 'G'))
        return fail (std::string ("`") + std::string (text) + "` is not a chord");

    i = 1;
    const auto shift = spelling::readAccidentals (text, i);
    const auto rootPc = pitchClassOf (*letterPc + shift);

    auto suffix = text.substr (i);
    auto minorBase = false;

    // "Cm7" is minor; "Cmaj7" is not. Longest-match on the quality words first.
    if (suffix.size() >= 1 && suffix[0] == 'm' && suffix != "maj7" && suffix != "maj9"
        && suffix != "m7b5" && suffix != "m7" && suffix != "m9" && suffix != "m6")
    {
        minorBase = true;
        suffix = suffix.substr (1);
    }

    auto recognised = false;
    const auto& intervals = intervalsForSuffix (suffix, minorBase, &recognised);

    if (! recognised)
        return fail (std::string ("`") + std::string (suffix) + "` is not a chord quality");

    RootAndShape out;
    out.rootPc = rootPc;
    out.intervals = intervals;
    out.minorBase = minorBase;
    out.label = std::string (text);

    return out;
}

} // namespace

std::vector<int> scalePitchesBetween (const Key& key, int low, int high)
{
    std::vector<int> pitches;

    if (high < low)
        return pitches;

    for (auto pitch = std::max (low, lowestPitch); pitch <= std::min (high, highestPitch); ++pitch)
        if (isScaleTone (key, pitch))
            pitches.push_back (pitch);

    return pitches;
}

// ------------------------------------------------------------------------------

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> out;

    for (const auto interval : intervals)
    {
        const auto pc = pitchClassOf (rootPc + interval);

        if (std::find (out.begin(), out.end(), pc) == out.end())
            out.push_back (pc);
    }

    return out;
}

bool Chord::containsPitchClass (int pc) const noexcept
{
    const auto wanted = pitchClassOf (pc);

    for (const auto interval : intervals)
        if (pitchClassOf (rootPc + interval) == wanted)
            return true;

    return false;
}

std::optional<ResolvedChord> resolveChord (const ChordSymbol& symbol, const Key& key,
                                           std::string* failureReason)
{
    auto localKey = key;

    // A tonicisation resolves its target FIRST and then reads the primary
    // numeral in that target's key. `V7/iv` is the dominant of iv, not the
    // dominant of the home key with a note about iv.
    if (! symbol.of.empty())
    {
        std::string reason;
        const auto target = resolveRoot (symbol.of, key, &reason);

        if (! target.has_value())
        {
            if (failureReason != nullptr)
                *failureReason = reason;

            return std::nullopt;
        }

        // HARMONIC minor, not natural, when the target is minor. The point of
        // tonicising with a dominant is the leading tone, and natural minor
        // does not have one - in C major, V7/vi is E7, whose G# is the leading
        // tone of A. A melody handed A aeolian over that chord would keep
        // choosing G natural and fight the very note that makes it a dominant.
        localKey = Key { target->rootPc, target->minorBase ? Mode::harmonicMinor : Mode::major };
    }

    const auto shape = resolveRoot (symbol.root, localKey, failureReason);

    if (! shape.has_value())
        return std::nullopt;

    Chord chord;
    chord.rootPc = shape->rootPc;
    chord.intervals = shape->intervals;
    chord.label = shape->label;
    chord.inversion = symbol.inversion;

    if (symbol.inversion < 0 || (std::size_t) symbol.inversion >= chord.intervals.size())
    {
        if (failureReason != nullptr)
            *failureReason = "`" + shape->label + "` has no inversion "
                             + std::to_string (symbol.inversion);

        return std::nullopt;
    }

    chord.bassPc = pitchClassOf (chord.rootPc + chord.intervals[(std::size_t) symbol.inversion]);

    if (! symbol.of.empty())
        chord.label += "/" + std::string (symbol.of);

    return ResolvedChord { chord, localKey };
}

} // namespace dew::lang
