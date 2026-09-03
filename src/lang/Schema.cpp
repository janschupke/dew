#include "lang/Schema.h"

#include <algorithm>
#include <string>

namespace dew::lang
{

namespace
{

const std::vector<std::string_view> empty {};

/** Levenshtein, capped. Only ever run against a table of a dozen names when
    something has already gone wrong, so the naive implementation is right.
*/
int editDistance (std::string_view a, std::string_view b)
{
    std::vector<int> previous (b.size() + 1);
    std::vector<int> current (b.size() + 1);

    for (std::size_t j = 0; j <= b.size(); ++j)
        previous[j] = (int) j;

    for (std::size_t i = 1; i <= a.size(); ++i)
    {
        current[0] = (int) i;

        for (std::size_t j = 1; j <= b.size(); ++j)
            current[j] = std::min ({ previous[j] + 1,
                                     current[j - 1] + 1,
                                     previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });

        previous = current;
    }

    return previous[b.size()];
}

/** A suggestion is only useful if it is close. A third of the length, at least
    one and at most three, keeps "mixor" -> "mixer" while refusing to propose
    "seed" for "wobble".
*/
bool closeEnough (std::string_view candidate, std::string_view text, int distance)
{
    const auto longest = std::max (candidate.size(), text.size());
    const auto budget = std::clamp ((int) longest / 3, 1, 3);

    return distance <= budget;
}

std::string_view closestOf (const std::vector<std::string_view>& candidates,
                            std::string_view text)
{
    std::string_view best;
    auto bestDistance = 0;

    for (const auto& candidate : candidates)
    {
        const auto distance = editDistance (candidate, text);

        if (! closeEnough (candidate, text, distance))
            continue;

        if (best.empty() || distance < bestDistance)
        {
            best = candidate;
            bestDistance = distance;
        }
    }

    return best;
}

} // namespace

const char* nameOf (ValueKind kind) noexcept
{
    switch (kind)
    {
        case ValueKind::text:         return "a quoted string";
        case ValueKind::integer:      return "a whole number";
        case ValueKind::number:       return "a number";
        case ValueKind::tempo:        return "a tempo, like `96 bpm`";
        case ValueKind::meter:        return "a meter, like `4/4`";
        case ValueKind::grid:         return "`auto` or a number of steps per beat";
        case ValueKind::key:          return "a key, like `F minor`";
        case ValueKind::seed:         return "a number";
        case ValueKind::pitchRange:   return "a pitch range, like `C3..C5`";
        case ValueKind::bars:         return "a number of bars, like `8 bars`";
        case ValueKind::jitteredInt:  return "a number, optionally `+- n`";
        case ValueKind::voices:       return "a number of voices, like `4 voices`";
        case ValueKind::muteBudget:   return "a budget, like `1 of 4`";
        case ValueKind::spread:       return "a voicing spread";
        case ValueKind::motion:       return "a voice-leading motion";
        case ValueKind::contour:      return "a melodic contour";
        case ValueKind::strongRule:   return "a strong-beat rule";
        case ValueKind::articulation: return "an articulation";
        case ValueKind::lineSource:   return "a line source";
        case ValueKind::instrument:   return "an instrument";
        case ValueKind::rhythmRef:    return "the name of a declared rhythm";
        case ValueKind::voicingRef:   return "the name of a declared voicing";
        case ValueKind::harmonyRef:   return "the name of a declared harmony";
        case ValueKind::channelRef:   return "the name of a declared channel";
        case ValueKind::cadence:      return "a chord tone to end on";
        case ValueKind::rule:         return "`forbid`, or `soft` and a weight";
        case ValueKind::scope:        return "how often a choice is re-drawn";
    }

    return "a value";
}

const std::vector<std::string_view>& membersOf (ValueKind kind)
{
    static const std::vector<std::string_view> spreads {
        "close", "open", "drop2", "drop3", "shell", "rootless" };
    static const std::vector<std::string_view> motions { "smooth", "parallel", "fixed" };
    static const std::vector<std::string_view> contours {
        "arch", "rise", "fall", "flat", "wave" };
    static const std::vector<std::string_view> strongRules {
        "chord-tones", "scale-tones", "free" };
    static const std::vector<std::string_view> articulations { "legato", "detached" };
    static const std::vector<std::string_view> lineSources {
        "root", "root-fifth", "root-third-fifth" };
    static const std::vector<std::string_view> instruments { "synth" };
    static const std::vector<std::string_view> scopes {
        "note", "bar", "instance", "section", "song" };

    switch (kind)
    {
        case ValueKind::spread:       return spreads;
        case ValueKind::motion:       return motions;
        case ValueKind::contour:      return contours;
        case ValueKind::strongRule:   return strongRules;
        case ValueKind::articulation: return articulations;
        case ValueKind::lineSource:   return lineSources;
        case ValueKind::instrument:   return instruments;
        case ValueKind::scope:        return scopes;

        case ValueKind::text:
        case ValueKind::integer:
        case ValueKind::number:
        case ValueKind::tempo:
        case ValueKind::meter:
        case ValueKind::grid:
        case ValueKind::key:
        case ValueKind::seed:
        case ValueKind::pitchRange:
        case ValueKind::bars:
        case ValueKind::jitteredInt:
        case ValueKind::voices:
        case ValueKind::muteBudget:
        case ValueKind::rhythmRef:
        case ValueKind::voicingRef:
        case ValueKind::harmonyRef:
        case ValueKind::channelRef:
        case ValueKind::cadence:
        case ValueKind::rule:
            break;
    }

    return empty;
}

BlockKind blockKindFor (std::string_view keyword) noexcept
{
    if (keyword == "song")        return BlockKind::song;
    if (keyword == "channel")     return BlockKind::channel;
    if (keyword == "voicing")     return BlockKind::voicing;
    if (keyword == "rhythm")      return BlockKind::rhythm;
    if (keyword == "harmony")     return BlockKind::harmony;
    if (keyword == "section")     return BlockKind::section;
    if (keyword == "arrangement") return BlockKind::arrangement;
    if (keyword == "part")        return BlockKind::part;
    if (keyword == "melody")      return BlockKind::melody;
    if (keyword == "chords")      return BlockKind::chords;
    if (keyword == "line")        return BlockKind::line;
    if (keyword == "counterpoint") return BlockKind::counterpoint;
    if (keyword == "overrides")   return BlockKind::overrides;

    return BlockKind::unknown;
}

const char* nameOf (BlockKind kind) noexcept
{
    switch (kind)
    {
        case BlockKind::song:        return "song";
        case BlockKind::channel:     return "channel";
        case BlockKind::voicing:     return "voicing";
        case BlockKind::rhythm:      return "rhythm";
        case BlockKind::harmony:     return "harmony";
        case BlockKind::section:     return "section";
        case BlockKind::arrangement: return "arrangement";
        case BlockKind::part:        return "part";
        case BlockKind::melody:      return "melody";
        case BlockKind::chords:      return "chords";
        case BlockKind::line:        return "line";
        case BlockKind::counterpoint: return "counterpoint";
        case BlockKind::overrides:   return "overrides";
        case BlockKind::unknown:     return "unknown";
    }

    return "unknown";
}

const std::vector<BlockSpec>& schema()
{
    static const std::vector<BlockSpec> table {
        { BlockKind::song,
          { { "title", ValueKind::text,   false, false, "the song's name" },
            { "tempo", ValueKind::tempo,  true,  false, "beats per minute" },
            { "meter", ValueKind::meter,  true,  false, "beats per bar over the beat unit" },
            { "grid",  ValueKind::grid,   false, false,
              "steps per beat, or `auto` to derive it from the durations written" },
            { "key",   ValueKind::key,    true,  false, "the home key" },
            { "seed",  ValueKind::seed,   false, false, "the root of every random choice" } },
          {},
          "whole-song properties", true },

        { BlockKind::channel,
          { { "instrument", ValueKind::instrument,  false, false, "what plays this part" },
            { "mixer",      ValueKind::integer,     false, false, "the mixer track to route to" },
            { "range",      ValueKind::pitchRange,  false, false, "the pitches this channel may use" },
            { "velocity",   ValueKind::jitteredInt, false, true,  "0..127, optionally jittered" },
            { "octave",     ValueKind::integer,     false, true,  "octaves to shift by" } },
          {},
          "one instrument", true },

        { BlockKind::voicing,
          { { "size",     ValueKind::voices,     false, false, "how many voices sound" },
            { "spread",   ValueKind::spread,     false, false, "how the voices are laid out" },
            { "register", ValueKind::pitchRange, false, false, "where the voicing sits" },
            { "motion",   ValueKind::motion,     false, false, "how it moves from the chord before" },
            { "maxLeap",  ValueKind::integer,    false, false, "the largest jump one voice may make" } },
          {},
          "how a chord is laid out", true },

        { BlockKind::rhythm, {}, {}, "a cycle of durations", true },

        { BlockKind::harmony,
          { { "key", ValueKind::key, false, false, "the key these numerals are read in" } },
          {},
          "a chord progression", true },

        { BlockKind::section,
          { { "length",  ValueKind::bars,       true,  false, "how long this section is" },
            { "harmony", ValueKind::harmonyRef, false, false, "which progression it uses" } },
          { BlockKind::part, BlockKind::harmony },
          "a named span of bars", true },

        { BlockKind::part,
          { { "chords", ValueKind::voicingRef, false, false, "play the harmony, voiced" },
            { "line",   ValueKind::lineSource, false, false, "play a single line" },
            { "rhythm", ValueKind::rhythmRef,  false, true,  "which rhythm to use" },
            { "octave", ValueKind::integer,    false, true,  "octaves to shift by" } },
          { BlockKind::melody, BlockKind::counterpoint, BlockKind::rhythm },
          "what one channel plays in this section" },

        { BlockKind::melody,
          { { "rhythm",       ValueKind::rhythmRef,   false, true,  "which rhythm to use" },
            { "articulation", ValueKind::articulation, false, true, "how long each note sounds" },
            { "contour",      ValueKind::contour,     false, true,  "the shape of the line" },
            { "strong",       ValueKind::strongRule,  false, true,  "what may fall on a strong beat" },
            { "variance",     ValueKind::number,      false, true,
              "0 is the same every compile; above 0 explores, reproducibly" },
            { "mute",         ValueKind::muteBudget,  false, true,  "how many onsets become rests" },
            { "cadence",      ValueKind::cadence,     false, true,
              "which tone of the last chord to end on - `1`, or "
              "`choose [1 3 5] per instance`" },
            { "range",        ValueKind::pitchRange,  false, true,  "the pitches this melody may use" } },
          { BlockKind::rhythm },
          "a generated single-voice line" },

        { BlockKind::counterpoint,
          { { "rhythm",       ValueKind::rhythmRef,    false, true,  "which rhythm to use" },
            { "articulation", ValueKind::articulation, false, true,  "how long each note sounds" },
            { "range",        ValueKind::pitchRange,   false, true,  "the pitches this voice may use" },
            { "variance",     ValueKind::number,       false, true,
              "0 is the same every compile; above 0 explores, reproducibly" },

            // The rules, as a closed set. Open-ended ones would be a constraint
            // solver by another name, and completion could not offer them.
            { "parallel-fifths",  ValueKind::rule, false, true,
              "two voices moving in parallel into a fifth" },
            { "parallel-octaves", ValueKind::rule, false, true,
              "the same, into an octave or a unison" },
            { "direct-fifths",    ValueKind::rule, false, true,
              "both voices moving the same way INTO a fifth or an octave" },
            { "voice-crossing",   ValueKind::rule, false, true,
              "this voice passing through the one it answers" },
            { "dissonance-on-strong", ValueKind::rule, false, true,
              "a dissonant interval on a beat that carries weight" },
            { "leaps",            ValueKind::rule, false, true,  "how much a jump costs" },
            { "repeats",          ValueKind::rule, false, true,
              "how much repeating the same pitch costs" } },
          { BlockKind::rhythm },
          "a voice written against another" },

        { BlockKind::arrangement, {}, {}, "the order the sections play in", true },

        { BlockKind::overrides, {}, { BlockKind::part }, "per-instance changes" },
    };

    return table;
}

const BlockSpec* specFor (BlockKind kind) noexcept
{
    for (const auto& spec : schema())
        if (spec.kind == kind)
            return &spec;

    return nullptr;
}

const KeySpec* keySpecFor (BlockKind kind, std::string_view key) noexcept
{
    const auto* spec = specFor (kind);

    if (spec == nullptr)
        return nullptr;

    for (const auto& candidate : spec->keys)
        if (candidate.name == key)
            return &candidate;

    return nullptr;
}

std::string_view closestKeyTo (BlockKind kind, std::string_view text) noexcept
{
    const auto* spec = specFor (kind);

    if (spec == nullptr)
        return {};

    std::vector<std::string_view> names;
    names.reserve (spec->keys.size());

    for (const auto& key : spec->keys)
        names.push_back (key.name);

    return closestOf (names, text);
}

std::string_view closestMemberTo (ValueKind kind, std::string_view text) noexcept
{
    return closestOf (membersOf (kind), text);
}

} // namespace dew::lang
