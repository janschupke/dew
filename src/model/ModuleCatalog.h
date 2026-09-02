#pragma once

#include <optional>
#include <vector>

#include "model/EffectType.h"
#include "model/ParamSpec.h"

namespace dew
{

/** What one effect is: what it is called, what it is called in a file, and
    every parameter it has.

    The registry is an explicit table joined to the enum, not a set of
    self-registering objects. These are static libraries built without
    --whole-archive, so a `static Registrar r;` in an effect's own translation
    unit would be dropped by the linker and the type would vanish with no error
    anywhere. A table the compiler can count is worth more than the convenience.
*/
struct EffectDescriptor
{
    EffectType type;
    const char* id;            ///< "filter" - what a .dew stores
    const char* displayName;   ///< "Filter"
    const ParamSpec* params;
    int numParams;
};

/** Every effect, indexed by EffectType. */
const std::vector<EffectDescriptor>& effectDescriptors();

const EffectDescriptor& effectDescriptor (EffectType) noexcept;

/** The type a stored id names, or nothing.

    Returns an optional rather than falling back to `filter`, which is what the
    old string reader did: an unrecognised type silently became a low-pass
    filter, with no warning, and the project sounded wrong in a way nothing
    reported.
*/
std::optional<EffectType> effectTypeFor (juce::StringRef id);

juce::String effectTypeToString (EffectType);
juce::String effectTypeDisplayName (EffectType);

/** The parameters every effect has, whatever its type.

    Just `mix`, and it is deliberately not in the per-type tables: the host
    applies dry/wet identically for every type, which is what keeps "a fully dry
    slot is bit-exact passthrough" true in exactly one place.
*/
const std::vector<ParamSpec>& commonEffectParams();

/** Every parameter a slot of this type has, in the order a person should meet
    them: the type's own first, then the common ones.

    Deliberately not the order effectSpec() emits, which puts `mix` first
    because that is where the file has always had it. A file's key order and a
    panel's control order are different questions that happen to concern the
    same list.
*/
std::vector<ParamSpec> effectParamsFor (EffectType);

} // namespace dew
