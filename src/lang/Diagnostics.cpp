#include "lang/Diagnostics.h"

#include <algorithm>

namespace dew::lang
{

namespace
{

/** Characters, not bytes: a caret row is aligned against what the eye sees. */
std::size_t characterCount (std::string_view text)
{
    std::size_t count = 0;

    for (const auto byte : text)
        if ((static_cast<unsigned char> (byte) & 0xC0) != 0x80)
            ++count;

    return count;
}

std::string severityName (Severity severity)
{
    return severity == Severity::error ? "error" : "warning";
}

} // namespace

DiagnosticBag::DiagnosticBag (std::string_view source)
    : text (source)
    , index (source)
{
}

Diagnostic& DiagnosticBag::add (Severity severity, std::string code, std::string message,
                                SourceRange range, std::string primaryLabel)
{
    if (items.size() >= maxDiagnostics)
    {
        if (! saidTooMany)
        {
            saidTooMany = true;

            // Reported at the end of the source rather than at the hundredth
            // error's position: it is a fact about the file, not about a line.
            const auto end = (std::uint32_t) text.size();
            items.push_back ({ Severity::error,
                               "E999",
                               "too many errors; stopping here",
                               { end, end },
                               {},
                               {},
                               {},
                               {},
                               std::nullopt });
        }

        discarded = {};
        return discarded;
    }

    // An exact repeat is never worth saying twice.
    const auto duplicate = std::any_of (items.begin(), items.end(), [&] (const Diagnostic& d)
                                        { return d.code == code && d.primary == range; });

    if (duplicate)
    {
        discarded = {};
        return discarded;
    }

    if (severity == Severity::error)
    {
        // One error per line. The second is nearly always the first one's echo -
        // a parser resynchronising produces several - and a wall of them buries
        // the one that is actually the cause.
        const auto line = index.lineAt (range.begin);

        if (std::find (linesWithErrors.begin(), linesWithErrors.end(), line)
            != linesWithErrors.end())
        {
            discarded = {};
            return discarded;
        }

        linesWithErrors.push_back (line);
        ++errorCount;
    }

    items.push_back ({ severity,
                       std::move (code),
                       std::move (message),
                       range,
                       std::move (primaryLabel),
                       {},
                       {},
                       {},
                       std::nullopt });

    return items.back();
}

Diagnostic& DiagnosticBag::error (std::string code, std::string message, SourceRange range,
                                  std::string primaryLabel)
{
    return add (Severity::error, std::move (code), std::move (message), range,
                std::move (primaryLabel));
}

Diagnostic& DiagnosticBag::warning (std::string code, std::string message, SourceRange range,
                                    std::string primaryLabel)
{
    return add (Severity::warning, std::move (code), std::move (message), range,
                std::move (primaryLabel));
}

// ------------------------------------------------------------------------------

std::string render (const Diagnostic& diagnostic, std::string_view source,
                    std::string_view fileName, const LineIndex& index)
{
    const auto line = index.lineAt (diagnostic.primary.begin);
    const auto column = index.columnAt (diagnostic.primary.begin);

    const auto lineNumber = std::to_string (line);
    const std::string gutter (lineNumber.size(), ' ');

    std::string out;
    out += std::string (fileName) + ":" + lineNumber + ":" + std::to_string (column) + ": "
           + severityName (diagnostic.severity) + "[" + diagnostic.code + "]: " + diagnostic.message
           + "\n";

    const auto lineText = index.lineTextAt (diagnostic.primary.begin);

    out += " " + lineNumber + " | " + std::string (lineText) + "\n";

    // The caret row. Width is measured in characters over the SPAN, so a range
    // covering a multi-byte character gets one caret per character rather than
    // one per byte.
    const auto span = diagnostic.primary.textIn (source);
    const auto caretCount = std::max<std::size_t> (1, characterCount (span));

    out += " " + gutter + " | " + std::string ((std::size_t) column - 1, ' ')
           + std::string (caretCount, '^');

    if (! diagnostic.primaryLabel.empty())
        out += " " + diagnostic.primaryLabel;

    out += "\n";

    for (const auto& related : diagnostic.related)
    {
        const auto relatedLine = index.lineAt (related.range.begin);

        out += " " + gutter + " = note: " + related.label + " here:\n";
        out += " " + std::to_string (relatedLine) + " | "
               + std::string (index.lineTextAt (related.range.begin)) + "\n";
    }

    for (const auto& note : diagnostic.notes)
        out += " " + gutter + " = note: " + note + "\n";

    for (const auto& help : diagnostic.helps)
        out += " " + gutter + " = help: " + help + "\n";

    return out;
}

std::string renderAll (const DiagnosticBag& bag, std::string_view source, std::string_view fileName)
{
    std::string out;

    for (const auto& diagnostic : bag.all())
        out += render (diagnostic, source, fileName, bag.lines());

    return out;
}

} // namespace dew::lang
