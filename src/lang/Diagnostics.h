#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lang/Messages.h"
#include "lang/SourceRange.h"

namespace dew::lang
{

enum class Severity
{
    error,
    warning
};

/** A second place worth pointing at: "declared here", "conflicting literal here".

    Diagnostics that name both witnesses are the difference between a message you
    can act on and one you have to go looking for - the grid errors in particular
    are useless without it, because the conflict is between two durations that
    may be forty lines apart.
*/
struct Related
{
    SourceRange range;
    std::string label;
};

struct FixIt
{
    SourceRange range;
    std::string replacement;
    std::string title;
};

struct Diagnostic
{
    Severity severity = Severity::error;

    /** Stable, documented and greppable: E0xx lexical, E1xx syntax, E2xx name
        and type resolution, E3xx harmony and structure, E4xx grid, E5xx
        generation, W6xx warnings.
    */
    std::string code;

    /** One line, lower case, no trailing full stop. */
    std::string message;

    SourceRange primary;
    std::string primaryLabel;

    std::vector<Related> related;
    std::vector<std::string> notes;
    std::vector<std::string> helps;
    std::optional<FixIt> fix;
};

/** Collects diagnostics, and enforces the three rules that keep a broken file
    from producing a wall of noise.

    - At most one ERROR per source line. The second error on a line is almost
      always the first one's echo, and a parser recovering from a bad token
      routinely produces several.
    - Identical (code, range) pairs are dropped outright.
    - After `maxDiagnostics` the bag stops accepting and says so once.

    Warnings are exempt from the one-per-line rule: two different rules relaxing
    at the same bar are two facts, not one repeated.
*/
class DiagnosticBag
{
public:
    static constexpr std::size_t maxDiagnostics = 100;

    /** The locale every sentence this bag collects is written in.

        Held here, and here only, because a bag is exactly as long-lived as one
        compile - which is exactly as long as a language is a meaningful thing
        to have chosen. dew_lang holds no mutable global state, so there is
        nowhere else it COULD live without two compiles in one process being
        able to differ by something neither of them was given.
    */
    explicit DiagnosticBag (std::string_view source, Locale = referenceLocale);

    Locale locale() const noexcept
    {
        return activeLocale;
    }

    /** `id`'s sentence in this bag's locale.

        Every string a diagnostic carries goes through here - the message, the
        primary label, and the notes, helps and related labels a call site
        pushes onto the Diagnostic it gets back. A literal at any of those is
        a sentence no translator will ever see.
    */
    std::string text (Msg id) const
    {
        return msg (id, activeLocale);
    }

    std::string text (Msg id, const MsgArgs& arguments) const
    {
        return msg (id, arguments, activeLocale);
    }

    Diagnostic& add (Severity, std::string code, std::string message, SourceRange,
                     std::string primaryLabel = {});

    Diagnostic& error (std::string code, std::string message, SourceRange,
                       std::string primaryLabel = {});

    Diagnostic& warning (std::string code, std::string message, SourceRange,
                         std::string primaryLabel = {});

    bool hasErrors() const noexcept
    {
        return errorCount > 0;
    }
    std::size_t size() const noexcept
    {
        return items.size();
    }

    const std::vector<Diagnostic>& all() const noexcept
    {
        return items;
    }

    const LineIndex& lines() const noexcept
    {
        return index;
    }

private:
    /** Somewhere to put a rejected diagnostic so `add` can always return a
        reference. Never read; overwritten by the next rejection.
    */
    Diagnostic discarded;

    std::string_view source;
    Locale activeLocale = referenceLocale;
    LineIndex index;
    std::vector<Diagnostic> items;
    std::vector<int> linesWithErrors;
    std::size_t errorCount = 0;
    bool saidTooMany = false;
};

/** One diagnostic, rendered the way a compiler prints it:

        amber.score:41:22: error[E402]: needs a grid of 24 steps per beat
           41 |     rhythm { 1/8 1/16 1/32 }
              |                       ^^^^ requires stepsPerBeat divisible by 24
              = note: 24 = lcm(8, 3)
              = help: replacing `1/32` with `1/16` would reach a grid of 12

    The caret row is built from CHARACTER columns, so a line containing a
    multi-byte character still puts the carets under the token.
*/
std::string render (const Diagnostic&, std::string_view source, std::string_view fileName,
                    const LineIndex&);

std::string renderAll (const DiagnosticBag&, std::string_view source, std::string_view fileName);

} // namespace dew::lang
