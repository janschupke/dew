#pragma once

#include <string_view>
#include <vector>

#include "lang/Messages.h"

namespace dew::lang
{

/** What a value position accepts.

    A CLOSED enum, and every key in the language declares exactly one. That is
    the restriction the whole design rests on: completion is a table lookup
    rather than type inference, and the grid can be computed statically because
    the set of duration literals is collectible from the parse tree.
*/
enum class ValueKind
{
    text,          ///< "Amber"
    integer,       ///< 4
    number,        ///< 0.25
    tempo,         ///< 96 bpm
    meter,         ///< 4/4
    grid,          ///< auto, or 12
    key,           ///< F minor
    seed,          ///< 0x5EEDC0FFEE
    pitchRange,    ///< C3..C5
    bars,          ///< 8 bars
    jitteredInt,   ///< 72 +- 6
    voices,        ///< 4 voices
    muteBudget,    ///< 1 of 4
    spread,        ///< close | open | drop2 | drop3 | shell | rootless
    motion,        ///< smooth | parallel | fixed
    contour,       ///< arch | rise | fall | flat | wave
    strongRule,    ///< chord-tones | scale-tones | free
    articulation,  ///< legato | detached
    lineSource,    ///< root | root-fifth | root-third-fifth
    cadence,       ///< 1, or `choose [1 3 5] per instance`
    rule,          ///< forbid, or `soft 2.5`
    bassRule,      ///< from-inversion | root | any
    leapRule,      ///< `max 7 resolve step`
    alignment,     ///< bar | continuous
    transposeMode, ///< diatonic | chromatic
    scope,         ///< note | bar | instance | section | song
    instrument,    ///< synth
    rhythmRef,     ///< a declared rhythm's name
    voicingRef,    ///< a declared voicing's name, after `with`
    harmonyRef,    ///< a declared harmony's name
    channelRef     ///< a declared channel's name
};

/** What this kind accepts, as a noun phrase - "a tempo, like `96 bpm`".

    A Msg rather than a const char*, because this is PROSE: it is spliced into
    E204 and E207 and it is what the website's reference shows against every
    key. nameOf (BlockKind) beside it stays a const char* and must, because that
    is the KEYWORD - `song`, `channel` - which is language syntax and identical
    in every locale. The two overloads reading differently is the point.
*/
Msg nameOf (ValueKind) noexcept;

/** The values an enum-shaped kind accepts, for validation and completion.
    Empty for kinds whose values are not a fixed list.
*/
const std::vector<std::string_view>& membersOf (ValueKind);

/** Which block a spec describes. Nested kinds are here too, because `part` and
    `melody` are blocks with their own keys even though they are never top level.
*/
enum class BlockKind
{
    song,
    channel,
    voicing,
    rhythm,
    harmony,
    section,
    arrangement,
    part,
    melody,
    counterpoint,
    imitate,
    chords,
    line,
    overrides,
    unknown
};

BlockKind blockKindFor (std::string_view keyword) noexcept;

const char* nameOf (BlockKind) noexcept;

struct KeySpec
{
    std::string_view name;
    ValueKind kind;
    bool required = false;

    /** May a per-instance override in the arrangement set this?

        `length` and `harmony` may not: an instance that changed its length
        would be a different section, and saying so is much better than
        silently producing one.
    */
    bool overridable = false;

    Msg doc;
};

struct BlockSpec
{
    BlockKind kind;
    std::vector<KeySpec> keys;
    std::vector<BlockKind> children;
    Msg doc;

    /** May this block be written at the top level of a file?

        Declared here rather than worked out from the children lists. `part`,
        `melody` and `counterpoint` are nested and `overrides` is synthesised by
        the parser, so "a block nothing else contains" would call `overrides`
        top level - and completion offered `counterpoint` as a seventh top-level
        keyword the moment one was added, because the exclusion list was written
        by hand somewhere else.
    */
    bool topLevel = false;
};

/** The whole language, declared once. Completion, validation and the reference
    manual all read this table, so a key cannot exist without being completable
    and cannot be documented differently from how it is checked - the same
    argument ProjectSchema.h makes about serialisation.
*/
const std::vector<BlockSpec>& schema();

const BlockSpec* specFor (BlockKind) noexcept;

const KeySpec* keySpecFor (BlockKind, std::string_view key) noexcept;

/** The closest key of this block to `text`, for a "did you mean" - or empty
    when nothing is close enough to be worth suggesting.
*/
std::string_view closestKeyTo (BlockKind, std::string_view text) noexcept;

/** The closest member of an enum kind, same rule. */
std::string_view closestMemberTo (ValueKind, std::string_view text) noexcept;

} // namespace dew::lang
