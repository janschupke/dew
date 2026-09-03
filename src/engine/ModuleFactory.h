#pragma once

#include <memory>

#include "engine/Effects.h"
#include "engine/InstrumentModule.h"
#include "engine/Module.h"
#include "model/EffectType.h"

namespace dew
{

/** Makes the module for a type.

    An explicit switch joined to the enum, not a set of self-registering
    objects. These are static libraries built without --whole-archive, so a
    `static Registrar r;` in a module's own translation unit would be dropped by
    the linker and the effect would simply stop existing in a release build,
    with no error anywhere. A switch the compiler checks is worth more than the
    convenience: leaving a type out of it fails to compile, because
    -Wswitch-enum is on.

    Message thread - it allocates.
*/
std::unique_ptr<EffectModule> createEffectModule (EffectType);

/** Makes the module for an instrument kind.

    The same explicit switch createEffectModule is, and for the same reason.
    Before this the two concrete instruments were named in AudioEngine, which
    meant adding a third kind was a new member on ChannelInstruments and an edit
    to each of the five places that mentioned the other two.

    Message thread - it allocates.
*/
std::unique_ptr<InstrumentModule> createInstrumentModule (InstrumentType);

/** Runs one slot: dry/wet, then the module, fully wet.

    The host owns the mix, not the modules, so `mix` behaves identically for
    every type and the fully-dry early return - which EffectTests pins as
    bit-exact passthrough - exists once rather than six times. Shared with the
    tests deliberately: a test that applied its own dry/wet would be pinning its
    own arithmetic rather than the engine's.

    `dryScratch` must hold at least io.numSamples in two channels; if it does
    not, the slot runs fully wet.
*/
void processEffectSlot (EffectModule& module, const EffectParamBlock& params, EffectType type,
                        StereoView io, juce::AudioBuffer<float>& dryScratch) noexcept;

} // namespace dew
