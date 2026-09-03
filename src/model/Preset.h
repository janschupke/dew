#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** One saved sound: an instrument's, or one effect slot's.

    A preset is the pair Module.h says belongs to the DESCRIPTOR rather than to
    each module - "getStateInformation belongs to the descriptor, two free
    functions written once and generic over every type". `state` is what those
    functions produce and consume; everything above it says which descriptor to
    read it with.

    What a preset does NOT carry is as much of the design as what it does.
    Nothing here identifies a slot or a channel: not an effect's `id`, which
    keys its DSP unit in the pool and would have two slots fighting over one
    reverb tank; not a channel's name, colour or routing; and not its effect
    chain, whose ids are project-unique and would collide the moment a preset
    were loaded twice.
*/
struct Preset
{
    /** "instrument" or "effect". One envelope for both, which is where the
        symmetry between the two descriptors shows up in the file. */
    juce::String kind;

    /** The descriptor's id: "synth", "audio", "reverb". Resolved through
        instrumentTypeFor / effectTypeFor, so an unknown one is refused rather
        than guessed. */
    juce::String typeId;

    juce::String name;
    juce::String description;

    /** The parameters, in the shape stateFor produces: flat for an effect,
        grouped the way the project file groups them for an instrument. */
    juce::var state;

    bool isInstrument() const
    {
        return kind == "instrument";
    }
    bool isEffect() const
    {
        return kind == "effect";
    }
};

} // namespace dew
