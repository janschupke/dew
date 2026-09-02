#pragma once

#include <string_view>
#include <vector>

#include "lang/SourceRange.h"
#include "lang/TokenKind.h"

namespace dew::lang
{

/** One token of a value, kept RAW.

    The parser deliberately does not interpret values. `velocity 72 +- 6`,
    `range C3..C5`, `size 4 voices` and `key F minor` have four different
    shapes, and a parser that knew all of them would be a second copy of the
    schema. Interpretation happens once, in the resolver, driven by the
    ValueKind the schema declares for that key - which is also what makes
    completion a table lookup.
*/
struct Value
{
    TokenKind kind = TokenKind::word;
    SourceRange range;
    std::string_view text;
};

/** `key value...`, newline-terminated. */
struct Statement
{
    std::string_view key;
    SourceRange keyRange;
    SourceRange range;
    std::vector<Value> values;
};

/** One chord in a harmony body: `i`, `bVI x2`, `i^1`, `V7/iv 2 bars`.

    Assembled from several tokens - `V7` `^` `1` `/` `iv` is one chord and five
    tokens - because `^` and `/` have to be their own tokens for a diagnostic to
    point at them.
*/
struct ChordEntry
{
    SourceRange range;

    std::string_view root;          ///< "i", "bVII", "V7", "Cm7"
    SourceRange rootRange;

    bool hasInversion = false;
    int inversion = 0;

    std::string_view of;            ///< the target of a `/` tonicisation

    /** `x2` - a share of what is left over. Mutually exclusive with `duration`. */
    bool hasWeight = false;
    int weight = 1;

    /** `2 bars`, `1/4` - an exact length. Raw, for the resolver. */
    std::vector<Value> duration;

    /** A `|` written after this entry: an assertion that the running position
        is a bar line, not a separator.
    */
    bool barCheckAfter = false;
};

/** One entry in a rhythm body: a duration, a rest, or a tie. */
struct RhythmEntry
{
    enum class Kind
    {
        duration,
        rest,
        tie
    };

    SourceRange range;
    Kind kind = Kind::duration;
    std::string_view text;          ///< the ratio lexeme, for a duration
    int repeat = 1;                 ///< from a following `xN`
};

struct Block;

/** One entry in the arrangement: `verse`, `chorus x2`, `chorus x2 identical`,
    `verse as verse_b { ... }`.
*/
struct ArrangementEntry
{
    SourceRange range;

    std::string_view section;
    SourceRange sectionRange;

    /** From `as <label>`. Pins the instance so inserting another instance
        earlier cannot renumber it and re-roll its choices.
    */
    std::string_view label;

    int repeat = 1;
    bool identical = false;

    /** The per-instance override block, if one was written. A vector rather
        than an optional so Block stays incomplete here.
    */
    std::vector<Block> overrides;
};

/** A block: a keyword, an optional header, and a body.

    The header is every word between the keyword and the `{` - `pad` in
    `part pad {`, `against lead` in `counterpoint against lead {`. Kept as a
    list rather than named fields for the same reason values are: the parser
    should not know what any particular block's header means.
*/
struct Block
{
    std::string_view keyword;
    SourceRange keywordRange;
    SourceRange range;

    std::vector<Value> header;

    /** The first header word, which is a name for every block that has one. */
    std::string_view name() const noexcept
    {
        return header.empty() ? std::string_view {} : header.front().text;
    }

    SourceRange nameRange() const noexcept
    {
        return header.empty() ? keywordRange : header.front().range;
    }

    std::vector<Statement> statements;
    std::vector<Block> children;

    // Exactly one of these is used, chosen by the keyword.
    std::vector<ChordEntry> chords;
    std::vector<RhythmEntry> rhythm;
    std::vector<ArrangementEntry> arrangement;
};

struct Document
{
    std::vector<Block> blocks;
};

} // namespace dew::lang
