#pragma once

#include <juce_core/juce_core.h>

#include "i18n/Strings.h"

namespace dew
{

/** What a parameter is CALLED, and what its own control's label says.

    A lookup on the property IDENTIFIER rather than two more fields on
    ParamSpec, for the reason declaredRoleOf is one: every row in the catalog is
    positionally aggregate-initialised, so two StringIds in the middle of a row
    would make declaring a channel's volume spell out the intervening defaults
    to reach them - which is how a table stops being readable. It works because
    no identifier needs two spellings, and ParamNameTests holds it that way.

    `paramNameOf` is the automation picker's and the tooltip's; `paramCaptionOf`
    is the short one a knob paints under itself.
*/
StringId paramNameOf (const juce::Identifier& property) noexcept;
StringId paramCaptionOf (const juce::Identifier& property) noexcept;

/** Every identifier the table names, for the test that holds it exhaustive. */
juce::Array<const juce::Identifier*> namedParameters();

} // namespace dew
