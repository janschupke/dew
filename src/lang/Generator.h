#pragma once

#include "lang/Diagnostics.h"
#include "lang/Model.h"
#include "lang/Score.h"

namespace dew::lang
{

/** Turns the resolved declarations into notes.

    Decides the grid, walks the arrangement into instances, renders each one,
    de-duplicates identical patterns, and places the clips.

    Instances are numbered by occurrence of the section's NAME, never by
    position in the arrangement - so inserting a bridge between two verses
    leaves both verses exactly as they were. `as <label>` pins an instance
    outright, which is the escape hatch for the one case name-counting cannot
    cover: inserting a verse in the MIDDLE renumbers the ones after it.
*/
Score generate (const Model&, DiagnosticBag&);

} // namespace dew::lang
