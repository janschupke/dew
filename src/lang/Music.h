#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dew::lang
{

/** Pitch, scale and chord arithmetic. Pure, and the only place that knows what
    a roman numeral means.

    Pitch numbers are MIDI, and middle C is 60 = "C4" - which is not a free
    choice: PianoRollComponent::noteName spells a pitch as `pitch / 12 - 1`, so
    a range written `C3..C5` in a score has to mean the same rows the piano roll
    labels C3 and C5. Any other convention would put a score's notes an octave
    from where its author read them.
*/
inline constexpr int lowestPitch = 0;
inline constexpr int highestPitch = 127;

/** "C4" -> 60, "F#3" -> 54, "Bb2" -> 46. Nothing for anything else. */
std::optional<int> parsePitch (std::string_view) noexcept;

/** The name the piano roll would print. Sharps, never flats, because that is
    what PianoRollComponent does.
*/
std::string pitchName (int pitch);

/** 0..11, from a pitch. */
constexpr int pitchClassOf (int pitch) noexcept
{
    return ((pitch % 12) + 12) % 12;
}

// ------------------------------------------------------------------------------

enum class Mode
{
    major,
    minor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    harmonicMinor,
    melodicMinor,
    majorPentatonic,
    minorPentatonic,
    blues,
    chromatic
};

std::optional<Mode> parseMode (std::string_view) noexcept;

const char* nameOf (Mode) noexcept;

/** The mode's semitone offsets from its tonic. */
const std::vector<int>& degreesOf (Mode);

/** True for the seven-note modes. A roman numeral names a scale DEGREE, so a
    pentatonic or a blues scale cannot carry one - and saying so is much better
    than silently indexing past the end of a five-note table.
*/
bool supportsRomanNumerals (Mode) noexcept;

struct Key
{
    int tonicPc = 0;
    Mode mode = Mode::major;
};

/** "F" + "minor". The tonic is a note name without an octave. */
std::optional<Key> parseKey (std::string_view tonic, std::string_view mode) noexcept;

bool isScaleTone (const Key&, int pitch) noexcept;

/** Every scale pitch in [low, high], ascending. What a melody chooses from. */
std::vector<int> scalePitchesBetween (const Key&, int low, int high);

// ------------------------------------------------------------------------------

/** A chord as intervals above a root, plus which of them is in the bass.

    Intervals rather than pitch classes so that a ninth stays a ninth: voicing
    needs to know that 14 is an octave above the second, because dropping the
    fifth of a ninth chord and dropping its second are different chords.
*/
struct Chord
{
    int rootPc = 0;
    int bassPc = 0;
    int inversion = 0;
    std::vector<int> intervals; ///< semitones above the root, ascending from 0
    std::string label;          ///< "Fm7", for the UI and for diagnostics

    /** The distinct pitch classes, root first. */
    std::vector<int> pitchClasses() const;

    bool containsPitchClass (int pc) const noexcept;
};

/** What the parser assembles from several tokens: `V7` `^` `1` `/` `iv` is one
    chord, but four tokens, and the pieces arrive separately.
*/
struct ChordSymbol
{
    std::string_view root; ///< "i", "bVII", "V7", "Cm7", "F#dim7"
    int inversion = 0;     ///< from `^N`
    std::string_view of;   ///< from `/X` - a numeral, for tonicisation
};

struct ResolvedChord
{
    Chord chord;

    /** The key a melody should draw scale tones from over this chord's span.

        Equal to the home key except under a tonicisation, where `V7/iv` has to
        put the melody in the key of iv or its scale tones fight the chord. One
        field, and it is what makes a secondary dominant sound like one rather
        than like wrong notes.
    */
    Key localKey;
};

/** Resolves a chord symbol against a key.

    Returns nothing when the symbol is not a chord at all; `failureReason` says
    why, in the voice a diagnostic wants ("`vii` needs a seven-note mode").
*/
std::optional<ResolvedChord> resolveChord (const ChordSymbol&, const Key&,
                                           std::string* failureReason = nullptr);

} // namespace dew::lang
