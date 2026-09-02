#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "model/Constants.h"
#include "model/EffectType.h"
#include "model/ModuleCatalog.h"

namespace dew
{

/** One slot's parameter values, by index.

    It was a struct with every field of every type at once - seventeen floats,
    of which a given slot used three or four. That was a reasonable answer when
    a switch statement dispatched on the type and could name the fields it
    wanted; it stops being one when each effect is a module that knows only its
    own parameters.

    Index 0 is `mix`, for every type, which is what lets the host apply dry/wet
    without asking a module where its mix lives. The type's own parameters
    follow in the order its descriptor declares them, and the catalog owns that
    order - see effectParamIndex.

    The filter's mode is a float here, holding its index, the way a discrete
    parameter is a float in every plugin API.
*/
using EffectParamBlock = std::array<float, kMaxEffectParams>;


} // namespace dew
