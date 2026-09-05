#pragma once

#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "model/PresetCategory.h"

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

    /** What the FILE says this is called, in English, written by the factory.

        A person reads PresetLibrary::displayName instead, which resolves the
        same preset by its id in their own locale. This stays because the format
        is meant to be opened and diffed: a file whose name field said
        `preset.warmPad.name` would be a file only the program can read.
    */
    juce::String name;
    juce::String description;

    /** What this preset is FOR, which is what a picker groups by.

        Stored as the category's id, never its label, for the reason `typeId` is
        - see PresetCategory. Empty for a preset written before the format
        carried one, and for one whose category this dew does not know; a picker
        offers those ungrouped rather than inventing a home for them.
    */
    std::optional<PresetCategory> category;

    /** The parameters, in the shape stateFor produces: flat for an effect,
        grouped the way the project file groups them for an instrument. */
    juce::var state;

    /** The file this came from - "warm-pad.dewpreset" - or empty for one that
        was never in the library.

        Set by PresetLibrary::all() from the factory's entry, and NOT part of
        the format: it is the identity a .dewpreset has by virtue of where it
        lives, which is why it is a field here rather than a key in the JSON.
        It is what a translated name is looked up by.

        Last, after everything the FILE holds, so that an aggregate initialiser
        naming the format's fields still reads as the format.
    */
    juce::String id;

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
