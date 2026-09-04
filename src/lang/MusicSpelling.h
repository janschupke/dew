#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace dew::lang::spelling
{

/** Reading a note name: its letter, and the sharps or flats after it.

    Shared by the two halves of what used to be Music.cpp. parsePitch and
    parseKey spell a note; resolveRoot spells the root of a chord symbol. One
    idea of what "Bb" means, in one place.
*/

inline constexpr int letterOffsets[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G

inline std::optional<int> letterToPitchClass (char c) noexcept
{
    const auto upper = (c >= 'a' && c <= 'g') ? (char) (c - 'a' + 'A') : c;

    if (upper < 'A' || upper > 'G')
        return std::nullopt;

    return letterOffsets[upper - 'A'];
}

/** Reads any run of 'b' and '#' as a signed semitone shift. */
inline int readAccidentals (std::string_view text, std::size_t& i) noexcept
{
    auto shift = 0;

    while (i < text.size() && (text[i] == 'b' || text[i] == '#'))
        shift += text[i++] == '#' ? 1 : -1;

    return shift;
}

inline std::string describePitchClass (int pc, bool preferFlat)
{
    static const char* sharps[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static const char* flats[] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };

    const auto index = ((pc % 12) + 12) % 12;
    return preferFlat ? flats[index] : sharps[index];
}

} // namespace dew::lang::spelling
