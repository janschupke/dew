#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/ModuleCatalog.h"

namespace dew
{

/** A module's parameters, as the var a preset carries.

    The pair Module.h says belongs to the DESCRIPTOR rather than to each module:
    a dew module owns no state the document owns - every parameter lives in the
    ValueTree and DSP state is by definition not persisted - so serialisation is
    generic over the type and written once, rather than a virtual pair that
    eight classes would implement identically and one of them would eventually
    get wrong.

    An effect's state is flat, because an effect is one node: one key per
    declared parameter, the type's own followed by mix. An instrument's is
    grouped exactly as the project file groups it, so a preset body can be read
    straight out of a .dew - an array of oscillator slots beside an envelope
    object, keyed as the schema keys them.

    (Deliberately described rather than shown. A source gate forbids spelling a
    parameter's name as a string literal outside Ids.h, and it reads comments
    too - which is right: an example that drifts is worse than no example.)

    Only DECLARED parameters appear, and only from groups the descriptor marks
    as a preset's - so a channel's name, colour, routing and level are absent by
    construction rather than by a list of exceptions somebody has to maintain.
*/
juce::var stateFor (const EffectDescriptor&, const juce::ValueTree& effect);
juce::var stateFor (const InstrumentDescriptor&, const juce::ValueTree& channel);

/** `state`, with every value coerced to its declared type and clamped to its
    declared range, everything the type does not declare dropped, and every
    parameter the state omits filled in from its default.

    Warnings rather than a hard failure, for the reason the schema warns: a
    preset written before a parameter existed should load as a preset that
    predates it, not as a file that will not open. Filling the omissions is what
    makes loading a one-oscillator preset over a three-oscillator patch silence
    the other two rather than leave them sounding.
*/
juce::var validateState (const EffectDescriptor&, const juce::var& state,
                         juce::StringArray& warnings);
juce::var validateState (const InstrumentDescriptor&, const juce::var& state,
                         juce::StringArray& warnings);

} // namespace dew
