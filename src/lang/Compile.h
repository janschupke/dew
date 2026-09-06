#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lang/Diagnostics.h"
#include "lang/Model.h"
#include "lang/Score.h"

namespace dew::lang
{

struct CompileResult
{
    /** Absent whenever anything was an error. A partial score is worse than
        none: it would render, sound wrong, and give no reason.
    */
    std::optional<Score> score;

    std::vector<Diagnostic> diagnostics;

    /** Always populated, even when the compile failed - completion in a file
        that does not yet compile is the only file anyone is ever editing.
    */
    SymbolTable symbols;

    bool ok() const noexcept
    {
        return score.has_value();
    }

    int errorCount() const noexcept;

    /** The diagnostics, rendered the way a compiler prints them. */
    std::string report (std::string_view source, std::string_view fileName) const;
};

/** Lex, parse, resolve, generate. The one entry point, and it never throws.

    Every diagnostic it produces is written in `locale`. The default is the
    REFERENCE, and that default is what keeps the committed examples
    byte-pinned: the three demos built from a score pass nothing, so they
    are English however the machine that ran it was set. The score editor passes
    the application's locale; dew_score and the MCP endpoint do not, because
    their output is a compiler-format report read by tools and by tests.

    No fileName. There was one, defaulted to "score", and the body's first
    statement was `(void) fileName;` - so seventeen call sites computed a name
    for it, four of them from a real document, and nothing read any of them. The
    name belongs to `CompileResult::report`, which prints it, and that is where
    it was already being passed a second time.
*/
CompileResult compile (std::string_view source, Locale locale = referenceLocale);

} // namespace dew::lang
