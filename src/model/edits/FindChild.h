#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/Ids.h"

namespace dew::edits
{

/** The child of `parent` with this type and this id, or an invalid tree.

    File-local to ProjectEdits.cpp until that file became a directory. Two of
    the units here look a node up by id - the finders, and the channel edits
    that re-route a removed channel - and a second copy of a loop this short is
    exactly the kind that gets one of its two conditions changed.
*/
inline juce::ValueTree findChildWithId (const juce::ValueTree& parent, const juce::Identifier& type,
                                        int id)
{
    for (const auto& child : parent)
        if (child.hasType (type) && (int) child[ids::id] == id)
            return child;

    return {};
}

} // namespace dew::edits
