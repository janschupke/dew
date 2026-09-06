#pragma once

#include "ui/design/Keys.h"

#include <vector>

namespace dew
{

/** The letter keys as a piano keyboard: which key is which semitone.

    Two overlapping rows, the layout every tracker and FL Studio itself uses.
    The bottom row is the lower octave and the number/top-letter row is one
    octave above it, so the two together reach two and a half octaves without a
    modifier and the same note can be played from either hand.

      z  s  x  d  c  v  g  b  h  n  j  m  ,  l  .  ;  /
      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16

      q  2  w  3  e  r  5  t  6  y  7  u  i  9  o  0  p
     12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28

    DELIBERATELY NOT a hotkeys:: table, and this is the load-bearing part.
    Every row in hotkeys::application() and hotkeys::timeline() is walked by
    HotkeyTests for collisions, and this map collides with the timeline's on
    purpose: `q` quantizes, `r` records, `1` `2` `3` pick tools and `0` `-` `=`
    zoom. It is a MODAL handler rather than a binding - the same argument
    ScoreEditorComponent's completion popup makes, and the same one the rules
    file records for it: while the mode is on this owns these keys and nothing
    outside it can reach them, and while the mode is off the map does not exist.
    A binding cannot say that, which is why this is not one.

    Only BARE keys are ever looked up here - TypingKeyboard checks the modifiers
    before it asks - so cmd-Z is still Undo, shift-R still opens Randomize, and
    every key with a modifier means exactly what it meant before.
*/
namespace typingKeys
{

/** One key and how far above the row's base note it sounds. */
struct Note
{
    int keyCode;
    int semitone;
};

/** The two keys that move the whole map an octave.

    Brackets because they are the only pair left that is adjacent, unmodified,
    free in every dew table, and NOT in the note map above - which rules out the
    z/x other trackers use, since both of those are notes here.
*/
inline constexpr int octaveDownKey = '[';
inline constexpr int octaveUpKey = ']';

/** How far the map may be moved. The upper row reaches 28 semitones above the
    base, so an octave above 7 would run off the top of MIDI. */
inline constexpr int lowestOctave = 0;
inline constexpr int highestOctave = 7;
inline constexpr int defaultOctave = 4;

/** The highest note the map itself can name, for the bound above. */
inline constexpr int highestSemitone = 28;

inline const std::vector<Note>& table()
{
    // A function-local static, the shape keys::valueKeys::table() already uses:
    // these are plain characters, but keeping the two tables the same shape is
    // what makes them read as two rows of one idea.
    static const std::vector<Note> rows {
        // The lower row, from the bottom-left key.
        { 'z', 0 },
        { 's', 1 },
        { 'x', 2 },
        { 'd', 3 },
        { 'c', 4 },
        { 'v', 5 },
        { 'g', 6 },
        { 'b', 7 },
        { 'h', 8 },
        { 'n', 9 },
        { 'j', 10 },
        { 'm', 11 },
        { ',', 12 },
        { 'l', 13 },
        { '.', 14 },
        { ';', 15 },
        { '/', 16 },

        // The upper row, an octave up. It overlaps the lower row's top five
        // notes on purpose: that is what lets a phrase cross the seam.
        { 'q', 12 },
        { '2', 13 },
        { 'w', 14 },
        { '3', 15 },
        { 'e', 16 },
        { 'r', 17 },
        { '5', 18 },
        { 't', 19 },
        { '6', 20 },
        { 'y', 21 },
        { '7', 22 },
        { 'u', 23 },
        { 'i', 24 },
        { '9', 25 },
        { 'o', 26 },
        { '0', 27 },
        { 'p', 28 },
    };

    return rows;
}

/** The MIDI note `semitone` is at, in `octave`, or -1 when that is off the top
    of the keyboard. Octave 4 puts `z` at middle C, which is 60. */
inline int pitchFor (int semitone, int octave) noexcept
{
    const auto pitch = 12 * (octave + 1) + semitone;

    return pitch >= 0 && pitch <= 127 ? pitch : -1;
}

/** Which semitone a key sounds, or -1 for a key that is not on the keyboard.

    Through keys::matches rather than a bare comparison, because that is where
    "the code, then the typed character, case-insensitively" is written down -
    and the punctuation keys at the end of each row are exactly the ones whose
    code and character disagree on some layouts. The stroke carries no
    modifiers, and matches() compares command, control and alt exactly, so a
    modified key never resolves here even if the caller forgets to check.
*/
inline int semitoneFor (const juce::KeyPress& key) noexcept
{
    for (const auto& note : table())
        if (keys::matches ({ note.keyCode, 0 }, key))
            return note.semitone;

    return -1;
}

} // namespace typingKeys

} // namespace dew
