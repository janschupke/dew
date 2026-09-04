#pragma once

#include "model/ParamRole.h"
#include "ui/design/Tokens.h"

/** Where a parameter's FUNCTION becomes a colour.

    The join between the two libraries, and the only place it happens. dew_model
    declares what a parameter does (ParamRole); dew_design declares what each
    function looks like (tokens::colour::func*); this says which is which, and
    nothing else in the application repeats it.

    Its own header rather than a function in Tokens.h, because Tokens.h is the
    vocabulary and knows nothing but JUCE. A design system that had to include
    the document model to state a colour would have stopped being one.
*/
namespace dew::palette
{

/** The colour a function is painted in.

    A switch with no default, so adding a ParamRole is a compile error here
    rather than a role that silently paints itself like everything else. That is
    the mechanism dew already uses for hotkeys::ViewCommand; this is the same
    trade, and it is the reason the enum is worth having at all.
*/
inline juce::Colour forRole (ParamRole role) noexcept
{
    switch (role)
    {
        case ParamRole::level: return tokens::colour::funcLevel;
        case ParamRole::stereo: return tokens::colour::funcStereo;
        case ParamRole::tone: return tokens::colour::funcTone;
        case ParamRole::time: return tokens::colour::funcTime;
        case ParamRole::space: return tokens::colour::funcSpace;
        case ParamRole::modulation: return tokens::colour::funcModulation;
        case ParamRole::pitch: return tokens::colour::funcPitch;

        // Multipurpose is not a function, so it takes the colour everything
        // multipurpose in dew takes.
        case ParamRole::generic: break;
    }

    return tokens::colour::accent;
}

/** Every role, in declaration order, for the one page that has to show them all.

    A page that listed six of seven would be a design system quietly hiding one,
    which is exactly what the gallery exists to prevent.
*/
inline constexpr ParamRole allRoles[] {
    ParamRole::generic, ParamRole::level, ParamRole::stereo,     ParamRole::tone,
    ParamRole::time,    ParamRole::space, ParamRole::modulation, ParamRole::pitch,
};

} // namespace dew::palette
