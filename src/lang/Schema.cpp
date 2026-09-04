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

        // clang-format off
        for (std::size_t j = 1; j <= b.size(); ++j)
            current[j] = std::min ({ previous[j] + 1,
                                     current[j - 1] + 1,
                                     previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });

        // clang-format on
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

// clang-format off
std::string_view closestOf (const std::vector<std::string_view>& candidates,
                            std::string_view text)
{
    std::string_view best;
    auto bestDistance = 0;

    // clang-format on
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

// clang-format off
Msg nameOf (ValueKind kind) noexcept
{
    switch (kind)
    {
        case ValueKind::text:         return Msg::kind_text_name;
        case ValueKind::integer:      return Msg::kind_integer_name;
        case ValueKind::number:       return Msg::kind_number_name;
        case ValueKind::tempo:        return Msg::kind_tempo_name;
        case ValueKind::meter:        return Msg::kind_meter_name;
        case ValueKind::grid:         return Msg::kind_grid_name;
        case ValueKind::key:          return Msg::kind_key_name;
        case ValueKind::seed:         return Msg::kind_seed_name;
        case ValueKind::pitchRange:   return Msg::kind_pitchRange_name;
        case ValueKind::bars:         return Msg::kind_bars_name;
        case ValueKind::jitteredInt:  return Msg::kind_jitteredInt_name;
        case ValueKind::voices:       return Msg::kind_voices_name;
        case ValueKind::muteBudget:   return Msg::kind_muteBudget_name;
        case ValueKind::spread:       return Msg::kind_spread_name;
        case ValueKind::motion:       return Msg::kind_motion_name;
        case ValueKind::contour:      return Msg::kind_contour_name;
        case ValueKind::strongRule:   return Msg::kind_strongRule_name;
        case ValueKind::articulation: return Msg::kind_articulation_name;
        case ValueKind::lineSource:   return Msg::kind_lineSource_name;
        case ValueKind::instrument:   return Msg::kind_instrument_name;
        case ValueKind::rhythmRef:    return Msg::kind_rhythmRef_name;
        case ValueKind::voicingRef:   return Msg::kind_voicingRef_name;
        case ValueKind::harmonyRef:   return Msg::kind_harmonyRef_name;
        case ValueKind::channelRef:   return Msg::kind_channelRef_name;
        case ValueKind::cadence:      return Msg::kind_cadence_name;
        case ValueKind::rule:         return Msg::kind_rule_name;
        case ValueKind::bassRule:     return Msg::kind_bassRule_name;
        case ValueKind::leapRule:     return Msg::kind_leapRule_name;
        case ValueKind::alignment:    return Msg::kind_alignment_name;
        case ValueKind::transposeMode: return Msg::kind_transposeMode_name;
        case ValueKind::scope:        return Msg::kind_scope_name;
    }

    // clang-format on
    // Unreachable while the switch is exhaustive, and -Wswitch-enum under the
    // ci preset is what keeps it so. A kind added to the enum fails to compile
    // above rather than falling through to this.
    return Msg::kind_unknown_name;
}

// clang-format off
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
    static const std::vector<std::string_view> bassRules {
        "from-inversion", "root", "any" };
    static const std::vector<std::string_view> alignments { "bar", "continuous" };
    static const std::vector<std::string_view> transposeModes { "diatonic", "chromatic" };

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
        case ValueKind::bassRule:     return bassRules;
        case ValueKind::alignment:    return alignments;
        case ValueKind::transposeMode: return transposeModes;

            // clang-format on
            // clang-format off
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
        case ValueKind::leapRule:
            break;
    }

    // clang-format on
    return empty;
}

// clang-format off
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
    if (keyword == "imitate")     return BlockKind::imitate;
    if (keyword == "overrides")   return BlockKind::overrides;

    // clang-format on
    return BlockKind::unknown;
}

// clang-format off
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
        case BlockKind::imitate:     return "imitate";
        case BlockKind::overrides:   return "overrides";
        case BlockKind::unknown:     return "unknown";
    }

    // clang-format on
    return "unknown";
}

// clang-format off
const std::vector<BlockSpec>& schema()
{
    static const std::vector<BlockSpec> table {
        { BlockKind::song,
          { { "title", ValueKind::text,   false, false, Msg::doc_songTitle_doc },
            { "tempo", ValueKind::tempo,  true,  false, Msg::doc_songTempo_doc },
            { "meter", ValueKind::meter,  true,  false, Msg::doc_songMeter_doc },
            { "grid",  ValueKind::grid,   false, false,
              Msg::doc_songGrid_doc },
            { "key",   ValueKind::key,    true,  false, Msg::doc_songKey_doc },
            { "seed",  ValueKind::seed,   false, false, Msg::doc_songSeed_doc } },
          {},
          Msg::doc_songBlock_doc, true },

        { BlockKind::channel,
          { { "instrument", ValueKind::instrument,  false, false, Msg::doc_channelInstrument_doc },
            { "mixer",      ValueKind::integer,     false, false, Msg::doc_channelMixer_doc },
            { "range",      ValueKind::pitchRange,  false, false, Msg::doc_channelRange_doc },
            { "velocity",   ValueKind::jitteredInt, false, true,  Msg::doc_channelVelocity_doc },
            { "octave",     ValueKind::integer,     false, true,  Msg::doc_sharedOctave_doc } },
          {},
          Msg::doc_channelBlock_doc, true },

        { BlockKind::voicing,
          { { "size",     ValueKind::voices,     false, false, Msg::doc_voicingSize_doc },
            { "spread",   ValueKind::spread,     false, false, Msg::doc_voicingSpread_doc },
            { "register", ValueKind::pitchRange, false, false, Msg::doc_voicingRegister_doc },
            { "motion",   ValueKind::motion,     false, false, Msg::doc_voicingMotion_doc },
            { "maxLeap",  ValueKind::integer,    false, false, Msg::doc_voicingMaxLeap_doc },
            { "bass",     ValueKind::bassRule,   false, false,
              Msg::doc_voicingBass_doc } },
          {},
          Msg::doc_voicingBlock_doc, true },

        { BlockKind::rhythm, {}, {}, Msg::doc_rhythmBlock_doc, true },

        { BlockKind::harmony,
          { { "key", ValueKind::key, false, false, Msg::doc_harmonyKey_doc } },
          {},
          Msg::doc_harmonyBlock_doc, true },

        { BlockKind::section,
          { { "length",  ValueKind::bars,       true,  false, Msg::doc_sectionLength_doc },
            { "harmony", ValueKind::harmonyRef, false, false, Msg::doc_sectionHarmony_doc } },
          { BlockKind::part, BlockKind::harmony },
          Msg::doc_sectionBlock_doc, true },

        { BlockKind::part,
          { { "chords", ValueKind::voicingRef, false, false, Msg::doc_partChords_doc },
            { "line",   ValueKind::lineSource, false, false, Msg::doc_partLine_doc },
            { "rhythm", ValueKind::rhythmRef,  false, true,  Msg::doc_sharedRhythm_doc },
            { "octave", ValueKind::integer,    false, true,  Msg::doc_sharedOctave_doc } },
          { BlockKind::melody, BlockKind::counterpoint, BlockKind::imitate,
            BlockKind::rhythm },
          Msg::doc_partBlock_doc },

        { BlockKind::melody,
          { { "rhythm",       ValueKind::rhythmRef,   false, true,  Msg::doc_sharedRhythm_doc },
            { "articulation", ValueKind::articulation, false, true, Msg::doc_sharedArticulation_doc },
            { "contour",      ValueKind::contour,     false, true,  Msg::doc_melodyContour_doc },
            { "strong",       ValueKind::strongRule,  false, true,  Msg::doc_melodyStrong_doc },
            { "variance",     ValueKind::number,      false, true,
              Msg::doc_sharedVariance_doc },
            { "mute",         ValueKind::muteBudget,  false, true,  Msg::doc_melodyMute_doc },
            { "leap",         ValueKind::leapRule,    false, true,
              Msg::doc_melodyLeap_doc },
            { "align",        ValueKind::alignment,   false, true,
              Msg::doc_sharedAlign_doc },
            { "cadence",      ValueKind::cadence,     false, true,
              Msg::doc_melodyCadence_doc },
            { "range",        ValueKind::pitchRange,  false, true,  Msg::doc_melodyRange_doc } },
          { BlockKind::rhythm },
          Msg::doc_melodyBlock_doc },

        { BlockKind::counterpoint,
          { { "rhythm",       ValueKind::rhythmRef,    false, true,  Msg::doc_sharedRhythm_doc },
            { "articulation", ValueKind::articulation, false, true,  Msg::doc_sharedArticulation_doc },
            { "range",        ValueKind::pitchRange,   false, true,  Msg::doc_counterpointRange_doc },
            { "variance",     ValueKind::number,       false, true,
              Msg::doc_sharedVariance_doc },
            { "align",        ValueKind::alignment,    false, true,
              Msg::doc_sharedAlign_doc },

            // The rules, as a closed set. Open-ended ones would be a constraint
            // solver by another name, and completion could not offer them.
            { "parallel-fifths",  ValueKind::rule, false, true,
              Msg::doc_counterpointParallelFifths_doc },
            { "parallel-octaves", ValueKind::rule, false, true,
              Msg::doc_counterpointParallelOctaves_doc },
            { "direct-fifths",    ValueKind::rule, false, true,
              Msg::doc_counterpointDirectFifths_doc },
            { "voice-crossing",   ValueKind::rule, false, true,
              Msg::doc_counterpointVoiceCrossing_doc },
            { "dissonance-on-strong", ValueKind::rule, false, true,
              Msg::doc_counterpointDissonanceOnStrong_doc },
            { "leaps",            ValueKind::rule, false, true,  Msg::doc_counterpointLeaps_doc },
            { "repeats",          ValueKind::rule, false, true,
              Msg::doc_counterpointRepeats_doc } },
          { BlockKind::rhythm },
          Msg::doc_counterpointBlock_doc },

        { BlockKind::imitate,
          { { "delay",     ValueKind::bars,          true,  true,
              Msg::doc_imitateDelay_doc },
            { "transpose", ValueKind::integer,       false, true,
              Msg::doc_imitateTranspose_doc },
            { "mode",      ValueKind::transposeMode, false, true,
              Msg::doc_imitateMode_doc } },
          {},
          Msg::doc_imitateBlock_doc },

        // clang-format on
        { BlockKind::arrangement, {}, {}, Msg::doc_arrangementBlock_doc, true },

        { BlockKind::overrides, {}, { BlockKind::part }, Msg::doc_overridesBlock_doc },
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
