// =============================================================================
// Pitch spelling, and the modes a key can be in.
//
// Split out of Music.cpp, which is now about turning a chord SYMBOL into
// pitches - the third and largest of the three strata Music.h rules off.
//
// This is the first two: a note name to a pitch class and back, and what a mode
// means as a set of degrees. Everything here is arithmetic on twelve semitones
// and nothing in it knows what a chord is.
// =============================================================================

#include "lang/Music.h"

#include "lang/MusicSpelling.h"

#include <algorithm>
#include <array>

#include "lang/ScanCore.h"

namespace dew::lang
{

// ------------------------------------------------------------------------------

std::optional<int> parsePitch (std::string_view text) noexcept
{
    if (text.empty())
        return std::nullopt;

    const auto letterPc = spelling::letterToPitchClass (text[0]);

    if (! letterPc.has_value() || ! (text[0] >= 'A' && text[0] <= 'G'))
        return std::nullopt;

    std::size_t i = 1;
    const auto shift = spelling::readAccidentals (text, i);

    if (i >= text.size())
        return std::nullopt;

    auto negative = false;

    if (text[i] == '-')
    {
        negative = true;
        ++i;
    }

    if (i >= text.size() || ! isDigit (text[i]))
        return std::nullopt;

    auto octave = 0;

    while (i < text.size() && isDigit (text[i]))
        octave = octave * 10 + (text[i++] - '0');

    if (i != text.size())
        return std::nullopt;

    if (negative)
        octave = -octave;

    // Middle C is 60 and is called C4, matching PianoRollComponent::noteName.
    const auto pitch = (octave + 1) * 12 + *letterPc + shift;

    if (pitch < lowestPitch || pitch > highestPitch)
        return std::nullopt;

    return pitch;
}

std::string pitchName (int pitch)
{
    return spelling::describePitchClass (pitchClassOf (pitch), false)
           + std::to_string (pitch / 12 - 1);
}

// ------------------------------------------------------------------------------

std::optional<Mode> parseMode (std::string_view text) noexcept
{
    struct Entry
    {
        const char* text;
        Mode mode;
    };

    static const Entry entries[] = {
        { "major", Mode::major },
        { "ionian", Mode::major },
        { "minor", Mode::minor },
        { "aeolian", Mode::minor },
        { "dorian", Mode::dorian },
        { "phrygian", Mode::phrygian },
        { "lydian", Mode::lydian },
        { "mixolydian", Mode::mixolydian },
        { "locrian", Mode::locrian },
        { "harmonic-minor", Mode::harmonicMinor },
        { "melodic-minor", Mode::melodicMinor },
        { "major-pentatonic", Mode::majorPentatonic },
        { "minor-pentatonic", Mode::minorPentatonic },
        { "blues", Mode::blues },
        { "chromatic", Mode::chromatic },
    };

    for (const auto& entry : entries)
        if (text == entry.text)
            return entry.mode;

    return std::nullopt;
}

const char* nameOf (Mode mode) noexcept
{
    switch (mode)
    {
        case Mode::major: return "major";
        case Mode::minor: return "minor";
        case Mode::dorian: return "dorian";
        case Mode::phrygian: return "phrygian";
        case Mode::lydian: return "lydian";
        case Mode::mixolydian: return "mixolydian";
        case Mode::locrian: return "locrian";
        case Mode::harmonicMinor: return "harmonic-minor";
        case Mode::melodicMinor: return "melodic-minor";
        case Mode::majorPentatonic: return "major-pentatonic";
        case Mode::minorPentatonic: return "minor-pentatonic";
        case Mode::blues: return "blues";
        case Mode::chromatic: return "chromatic";
    }

    return "major";
}

const std::vector<int>& degreesOf (Mode mode)
{
    static const std::vector<int> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::vector<int> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::vector<int> dorian { 0, 2, 3, 5, 7, 9, 10 };
    static const std::vector<int> phrygian { 0, 1, 3, 5, 7, 8, 10 };
    static const std::vector<int> lydian { 0, 2, 4, 6, 7, 9, 11 };
    static const std::vector<int> mixolydian { 0, 2, 4, 5, 7, 9, 10 };
    static const std::vector<int> locrian { 0, 1, 3, 5, 6, 8, 10 };
    static const std::vector<int> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    static const std::vector<int> melodicMinor { 0, 2, 3, 5, 7, 9, 11 };
    static const std::vector<int> majorPentatonic { 0, 2, 4, 7, 9 };
    static const std::vector<int> minorPentatonic { 0, 3, 5, 7, 10 };
    static const std::vector<int> blues { 0, 3, 5, 6, 7, 10 };
    static const std::vector<int> chromatic { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    switch (mode)
    {
        case Mode::major: return major;
        case Mode::minor: return minor;
        case Mode::dorian: return dorian;
        case Mode::phrygian: return phrygian;
        case Mode::lydian: return lydian;
        case Mode::mixolydian: return mixolydian;
        case Mode::locrian: return locrian;
        case Mode::harmonicMinor: return harmonicMinor;
        case Mode::melodicMinor: return melodicMinor;
        case Mode::majorPentatonic: return majorPentatonic;
        case Mode::minorPentatonic: return minorPentatonic;
        case Mode::blues: return blues;
        case Mode::chromatic: return chromatic;
    }

    return major;
}

bool supportsRomanNumerals (Mode mode) noexcept
{
    return degreesOf (mode).size() == 7;
}

std::optional<Key> parseKey (std::string_view tonic, std::string_view mode) noexcept
{
    if (tonic.empty())
        return std::nullopt;

    const auto letterPc = spelling::letterToPitchClass (tonic[0]);

    if (! letterPc.has_value() || ! (tonic[0] >= 'A' && tonic[0] <= 'G'))
        return std::nullopt;

    std::size_t i = 1;
    const auto shift = spelling::readAccidentals (tonic, i);

    if (i != tonic.size())
        return std::nullopt;

    const auto parsedMode = parseMode (mode);

    if (! parsedMode.has_value())
        return std::nullopt;

    return Key { pitchClassOf (*letterPc + shift), *parsedMode };
}

bool isScaleTone (const Key& key, int pitch) noexcept
{
    const auto offset = pitchClassOf (pitch - key.tonicPc);
    const auto& degrees = degreesOf (key.mode);

    return std::find (degrees.begin(), degrees.end(), offset) != degrees.end();
}
} // namespace dew::lang
