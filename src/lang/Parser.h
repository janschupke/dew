#pragma once

#include <string_view>

#include "lang/Ast.h"
#include "lang/Diagnostics.h"

namespace dew::lang
{

/** The seven top-level block keywords. Closed, and the parser's resync set. */
bool isTopLevelKeyword (std::string_view) noexcept;

/** Blocks whose body is a list of chords rather than key/value statements. */
bool hasChordBody (std::string_view keyword) noexcept;
bool hasRhythmBody (std::string_view keyword) noexcept;
bool hasArrangementBody (std::string_view keyword) noexcept;

/** Structure only: blocks, headers, statements and the three list bodies.

    Values are NOT interpreted here - `72 +- 6` comes out as three tokens, and
    what they mean is the resolver's question. So the only errors this reports
    are structural: an unclosed brace, a statement where a block was needed, a
    chord entry that is not a chord shape.

    Never throws, and always returns a Document. Every parse function yields a
    node even on failure, so the tree is complete-shaped for a half-typed file -
    which is what completion needs, and the reason not to use a generator that
    throws.

    ONE STATEMENT PER LINE. A statement runs to the end of its line, full stop.
    The alternative - ending a statement when the next word is a key the schema
    knows - would let `channel pad { instrument synth  mixer 1 }` sit on one
    line, but it puts the schema inside the parser and, worse, makes a typo
    disappear: `mixor 1` would be absorbed as two more values of the previous
    statement and reported as "too many values for instrument" rather than
    "unknown key `mixor`". Precise errors are the point, so the line wins.

    A block written entirely on one line therefore holds at most one statement,
    which keeps `channel pad { mixer 1 }` working and reads fine.

    Recovery rests on three anchors: statements end at a newline, so one bad
    statement costs one line; brace depth is tracked, so each unclosed `{` is
    reported once, at that brace, rather than as a cascade of unexpected tokens
    on every following line; and a top-level keyword at the start of a line
    always begins a new block, so even a catastrophic error costs at most one
    block.
*/
Document parse (std::string_view source, DiagnosticBag&);

} // namespace dew::lang
