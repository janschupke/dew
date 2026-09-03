#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dew::lang
{

/** The compiled score: the boundary between the language and its host.

    Deliberately shaped like dew's document rather than like the language -
    tracks, patterns of notes, and clips placing those patterns - so the thing
    that writes it into a ValueTree is a straight walk with no decisions left in
    it. Every decision that could be got wrong has already been made here, where
    it is testable without a project.

    Nothing in this file knows what a ValueTree is.
*/

struct Note
{
    int track = 0;     ///< an index into Score::tracks
    int startStep = 0; ///< relative to its pattern
    int lengthSteps = 1;
    int pitch = 60;
    float velocity = 0.75f; ///< 0..1, as dew stores it
};

struct TrackDesc
{
    std::string name;
    int mixerTrack = 1;
    int lowPitch = 24;
    int highPitch = 96;
};

struct PatternDesc
{
    std::string name;
    int lengthSteps = 16;
    std::vector<Note> notes; ///< sorted by step, then track, then pitch

    /** What this pattern IS, independently of where it landed.

        The same identity the RNG draws from - a section name plus either an
        occurrence ordinal or an explicit label - so a recompile can recognise
        the pattern it wrote last time even though the arrangement moved, and
        so `as <label>` pins the document node exactly as it pins the music.

        Not the name: two instances labelled the same way can hold different
        notes, and a name is something a user may edit.
    */
    std::string key;
};

struct ClipDesc
{
    int pattern = 0; ///< an index into Score::patterns
    int startBar = 0;
    int lengthBars = 1;
    std::string label; ///< the instance it came from, for reporting
    std::string key;   ///< as PatternDesc::key, plus which placement
};

/** A chord and where it sounds, in steps from the start of the song. For the
    editor to show, and for a test to read without re-deriving it.
*/
struct ChordSpanOut
{
    int startStep = 0;
    int endStep = 0;
    std::string label;
};

struct Score
{
    std::string title = "Untitled";
    double tempoBpm = 120.0;

    int stepsPerBeat = 4;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int barsInSong = 0;

    std::uint64_t seed = 0;

    std::vector<TrackDesc> tracks;
    std::vector<PatternDesc> patterns;
    std::vector<ClipDesc> clips;
    std::vector<ChordSpanOut> harmony;

    int stepsPerBar() const noexcept
    {
        return stepsPerBeat * beatsPerBar;
    }

    /** Every note, at its absolute position in the song, with clip repeats
        expanded. What a golden file and a MIDI comparison want; the patterns
        and clips are what the document wants.
    */
    std::vector<Note> flatten() const;

    /** Notes as PLAYED - a pattern placed by two clips counts twice. The
        number of notes STORED is the sum over `patterns`, which is smaller
        whenever a section repeats identically.
    */
    int noteCount() const noexcept;
};

} // namespace dew::lang
