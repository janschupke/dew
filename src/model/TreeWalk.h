#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/Ids.h"

namespace dew::tree
{

/** The four walks over a ValueTree's children that the tree kept rewriting.

    Each is three or four lines, which is exactly why they spread: too short to
    read as duplication, and each with two conditions where changing one and not
    the other is a silent bug. findChildWithId lived under model/edits and said
    so in its own comment - "a second copy of a loop this short is exactly the
    kind that gets one of its two conditions changed" - while six copies of it
    stood in the engine, the mixer panel, the demo builders and the automation
    resolver, none of which could reach model/edits.

    Here, in dew_model, because every layer above links it. A walk over a
    document is not an EDIT to one, which is what kept it in the wrong place.
*/

/** The child of `parent` with this type and this id, or an invalid tree. */
inline juce::ValueTree childWithId (const juce::ValueTree& parent, const juce::Identifier& type,
                                    int id)
{
    for (const auto& child : parent)
        if (child.hasType (type) && (int) child[ids::id] == id)
            return child;

    return {};
}

/** The nth child of this type, counting only children of this type.

    Not parent.getChild (n): a document holds several kinds of child under one
    node - a project holds channels, patterns and automations together - so the
    nth EFFECT is not the nth child, and a clip that stores a slot number is
    asking for the first of those.
*/
inline juce::ValueTree nthChildOfType (const juce::ValueTree& parent, const juce::Identifier& type,
                                       int n)
{
    auto index = 0;

    for (const auto& child : parent)
    {
        if (! child.hasType (type))
            continue;

        if (index == n)
            return child;

        ++index;
    }

    return {};
}

/** Where `child` sits among the children of its type, or -1. The inverse of
    nthChildOfType, and the pair has to agree about what it counts. */
inline int indexOfChildOfType (const juce::ValueTree& parent, const juce::Identifier& type,
                               const juce::ValueTree& child)
{
    auto index = 0;

    for (const auto& candidate : parent)
    {
        if (! candidate.hasType (type))
            continue;

        if (candidate == child)
            return index;

        ++index;
    }

    return -1;
}

/** The same, by id rather than by identity - which is what a caller holding a
    number from a document has. */
inline int indexOfChildWithId (const juce::ValueTree& parent, const juce::Identifier& type, int id)
{
    return indexOfChildOfType (parent, type, childWithId (parent, type, id));
}

} // namespace dew::tree
