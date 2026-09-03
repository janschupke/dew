#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lang/Grid.h"
#include "lang/Music.h"
#include "lang/Schema.h"
#include "lang/SourceRange.h"

namespace dew::lang
{

/** The declarations, resolved and typed.

    Everything here has been checked: names bind, values are the kind the schema
    declared, pitches are in range. The generator reads only this, so it never
    has to ask whether something was written correctly - which keeps the
    "is this legal" question in one pass and the "what does it sound like"
    question in another.
*/

struct SongSpec
{
    std::string title = "Untitled";
    double tempoBpm = 120.0;
    TimeSignature meter;
    Key key;
    std::uint64_t seed = 0;

    /** Explicit `grid N` is a contract the compiler checks; `auto` lets it
        derive one. The difference matters when applying to an existing project,
        because stepsPerBeat owns how long a step is.
    */
    bool gridIsAuto = true;
    int declaredGrid = 0;
    SourceRange gridRange;
};

/** How often a random choice is re-drawn.

    Written `per <scope>` after the value it applies to. The scope IS the
    identity of the draw: a choice `per instance` is one draw for the whole
    instance, so it cannot change halfway through and cannot be moved by an edit
    somewhere else in the file.
*/
enum class Scope
{
    note,      ///< a fresh draw at every onset
    bar,       ///< one per bar
    instance,  ///< one per rendered section instance - the default for a choice
    section,   ///< one per section, shared by all of its instances
    song       ///< one for the whole song
};

/** A value that is chosen from a list rather than set.

    Deliberately narrow: a list of chord-tone degrees, not an expression. The
    moment a value can be computed, completion stops being a table lookup and
    the grid stops being statically knowable - which is the constraint the whole
    language rests on.
*/
struct DegreeChoice
{
    std::vector<int> degrees;          ///< 1, 3, 5, 7 - which tone of the chord
    Scope scope = Scope::instance;

    bool declared() const noexcept { return ! degrees.empty(); }
};

struct ChannelSpec
{
    std::string name;
    SourceRange range;

    int mixerTrack = 1;
    int lowPitch = 24;
    int highPitch = 96;
    int velocity = 96;
    int velocityJitter = 0;

    /** How often the jitter is re-drawn. Per note by default, which is what
        makes a held chord sound played rather than typed.
    */
    Scope velocityScope = Scope::note;
    int octave = 0;
};

enum class Spread { close, open, drop2, drop3, shell, rootless };
enum class Motion { smooth, parallel, fixed };
enum class Contour { arch, rise, fall, flat, wave };
enum class StrongRule { chordTones, scaleTones, free };
enum class Articulation { legato, detached };
enum class LineSource { root, rootFifth, rootThirdFifth };

struct VoicingSpec
{
    std::string name;
    SourceRange range;

    int voices = 4;
    Spread spread = Spread::close;
    int lowPitch = 48;
    int highPitch = 72;
    Motion motion = Motion::smooth;
    int maxLeap = 7;
};

struct RhythmStep
{
    Duration duration;
    bool isRest = false;
    bool isTie = false;
    SourceRange range;
};

struct RhythmSpec
{
    std::string name;
    SourceRange range;
    std::vector<RhythmStep> steps;
};

struct ChordSpec
{
    ChordSymbol symbol;
    SourceRange range;

    bool hasWeight = false;
    int weight = 1;

    /** An exact length in the smallest unit the section can express. Only one
        of `weight` and `bars`/`duration` is set.
    */
    std::optional<int> bars;
    std::optional<Duration> duration;

    bool barCheckAfter = false;
};

struct HarmonySpec
{
    std::string name;
    SourceRange range;
    std::optional<Key> key;
    std::vector<ChordSpec> chords;
};

enum class PartKind { chords, line, melody };

struct MelodySpec
{
    std::string rhythm;
    Articulation articulation = Articulation::legato;
    Contour contour = Contour::arch;
    StrongRule strong = StrongRule::chordTones;
    float variance = 0.0f;
    int muteCount = 0;
    int muteWindow = 0;
    bool hasRange = false;
    int lowPitch = 0;
    int highPitch = 0;

    /** Which tone of the last chord the line ends on.

        `cadence 1` always ends on the root; `cadence choose [1 3 5] per
        instance` ends on one of the three, the same one every compile, a
        different one in each instance. This is the "ending note" a seed is
        supposed to be able to vary.
    */
    DegreeChoice cadence;
};

struct PartSpec
{
    std::string channel;
    SourceRange range;

    PartKind kind = PartKind::chords;

    std::string voicing;          ///< for chords
    LineSource lineSource = LineSource::root;
    std::string rhythm;           ///< a named rhythm, or empty for an inline one
    std::optional<RhythmSpec> inlineRhythm;
    int octave = 0;

    MelodySpec melody;
};

struct SectionSpec
{
    std::string name;
    SourceRange range;

    int bars = 4;
    std::string harmony;                    ///< a named harmony, or empty
    std::optional<HarmonySpec> inlineHarmony;
    std::vector<PartSpec> parts;
};

struct ArrangementItem
{
    std::string section;
    SourceRange range;

    std::string label;      ///< from `as`, pinning the instance
    int repeat = 1;
    bool identical = false;

    std::vector<PartSpec> overrides;
};

struct Model
{
    SongSpec song;
    std::vector<ChannelSpec> channels;
    std::vector<VoicingSpec> voicings;
    std::vector<RhythmSpec> rhythms;
    std::vector<HarmonySpec> harmonies;
    std::vector<SectionSpec> sections;
    std::vector<ArrangementItem> arrangement;

    /** Every duration literal written anywhere, so the grid can be resolved
        from the whole file rather than one block at a time.
    */
    std::vector<DurationUse> durationUses;

    const ChannelSpec* channel (std::string_view) const noexcept;
    const VoicingSpec* voicing (std::string_view) const noexcept;
    const RhythmSpec* rhythm (std::string_view) const noexcept;
    const HarmonySpec* harmony (std::string_view) const noexcept;
    const SectionSpec* section (std::string_view) const noexcept;
};

/** Names in scope, for completion. Populated even when resolution failed -
    completion in a broken file is the case that matters.
*/
struct SymbolTable
{
    std::vector<std::string> channels;
    std::vector<std::string> voicings;
    std::vector<std::string> rhythms;
    std::vector<std::string> harmonies;
    std::vector<std::string> sections;
};

} // namespace dew::lang
