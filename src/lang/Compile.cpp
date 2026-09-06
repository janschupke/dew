#include "lang/Compile.h"

#include "lang/Generator.h"
#include "lang/Parser.h"
#include "lang/Resolver.h"

namespace dew::lang
{

int CompileResult::errorCount() const noexcept
{
    auto count = 0;

    for (const auto& d : diagnostics)
        if (d.severity == Severity::error)
            ++count;

    return count;
}

std::string CompileResult::report (std::string_view source, std::string_view fileName) const
{
    const LineIndex index { source };
    std::string out;

    for (const auto& d : diagnostics)
        out += render (d, source, fileName, index);

    return out;
}

CompileResult compile (std::string_view source, Locale locale)
{
    CompileResult result;
    DiagnosticBag bag { source, locale };

    const auto document = parse (source, bag);
    const auto model = resolve (document, source, bag, result.symbols);

    // Generation runs only on a document that resolved. Its inputs are names
    // that bind and values of the declared kind, and asking it to work without
    // those would mean every generator carrying its own "was this checked"
    // branch - which is how the two passes would drift.
    if (! bag.hasErrors())
    {
        auto score = generate (model, bag);

        if (! bag.hasErrors())
            result.score = std::move (score);
    }

    result.diagnostics = bag.all();
    return result;
}

} // namespace dew::lang
