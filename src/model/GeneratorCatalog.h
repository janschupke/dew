#pragma once

#include <optional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "model/ParamSpec.h"

namespace dew
{

/** Which generator an oscillator slot runs, as a declared table.

    Wavetable was not an instrument and not a slot's own kind either: it was a
    `mode` row in the middle of one thirteen-column parameter table, with the
    classic half and the wavetable half either side of it and nothing saying
    which was which. Five things had to know the split and each knew it
    separately - the panel by a `bool showingWavetable` and a string literal,
    automation by a mode check that gated the WHOLE scope, the engine by two
    parallel arrays, the preset library not at all, and the UI by a third copy
    of the choice table.

    So a generator is a descriptor, in the same shape and for the same reason
    InstrumentDescriptor is one: given either, ONE walk enumerates every
    parameter of a kind, reads its state and writes it back. A third generator
    is a row here plus a class in the engine, rather than an eighth thing to
    remember.

    What this deliberately does NOT do is give each generator a node of its own.
    ProjectSchema's own comment argues the other way and it is right: one
    declared table stays one walk in the reader, and every channel already
    carries every instrument kind's node inert to keep the canonical tree one
    shape. A generator's parameters live on the OSC node beside the shared ones,
    and this table says which are whose.
*/
struct GeneratorDescriptor
{
    /** What a .dew stores in a slot's `mode`. Never translated. */
    const char* id;

    StringId displayName;

    /** The node under an oscillator slot that holds this generator's
        parameters: ids::CLASSIC, ids::WAVETABLE. Both are always present and
        one is inert, which is the shape every channel already has. */
    const juce::Identifier* node;

    /** The parameters this generator alone reads. The slot's shared five -
        enabled, wave-independent octave, detune, gain and the mode itself - are
        in neither table, because they belong to the SLOT. */
    const ParamSpec* params;
    int numParams;
};

/** Every generator, in the order the `mode` choice declares them. */
const std::vector<GeneratorDescriptor>& generatorDescriptors();

/** The generator a stored `mode` names, or nothing.

    An optional rather than a fallback, for the reason instrumentTypeFor is one:
    a file naming a generator this build does not have is a fact worth
    reporting, not one to guess at.
*/
std::optional<int> generatorIndexFor (juce::StringRef id);

/** The descriptor for a stored `mode`, falling back to the first - which is
    `classic`, and is what a slot with no opinion has always been. */
const GeneratorDescriptor& generatorFor (juce::StringRef id);

/** Everything a slot running this generator can be automated on: the shared
    parameters plus that generator's own.

    The mode gate this replaces cost a CLASSIC slot its `gain`, which the
    catalog has declared automatable since it was written - the whole scope
    returned nothing unless the slot was in wavetable mode.
*/
std::vector<ParamSpec> generatorParamSpecs (juce::StringRef id);

/** Whether `property` belongs to a generator OTHER than `id` - so a control, a
    picker or a snapshot can tell "not mine" from "not a parameter". */
bool isForeignGeneratorParam (juce::StringRef id, const juce::Identifier& property);

/** The node under `slot` that actually holds `property`.

    The slot itself for one of the SLOT's own five, and the owning generator's
    child node for anything else. One answer, asked by the engine's reader, by
    every control the panel builds, by the demo builders and by the parameter
    menu - so nesting a generator's parameters is not eight places that each
    have to remember the new depth.

    An invalid tree when the slot has no such child, which a caller writing
    through ProjectEdits will report rather than write into nothing.
*/
juce::ValueTree generatorNodeFor (const juce::ValueTree& slot, const juce::Identifier& property);

} // namespace dew
