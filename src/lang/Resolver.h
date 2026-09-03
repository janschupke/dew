#pragma once

#include "lang/Ast.h"
#include "lang/Diagnostics.h"
#include "lang/Model.h"

namespace dew::lang
{

/** Binds names, checks values against the schema, and produces the typed model.

    Two passes, deliberately. The first collects every declared name so that a
    reference may appear before its declaration and so the symbol table exists
    even when the second pass fails; the second interprets values and resolves
    references. That is what lets completion work in a file that does not yet
    compile, which is the only file anyone is ever editing.

    `symbols` is always populated. `Model` is only meaningful when the bag has
    no errors.
*/
Model resolve (const Document&, std::string_view source, DiagnosticBag&, SymbolTable& symbols);

} // namespace dew::lang
