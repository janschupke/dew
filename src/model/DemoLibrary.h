#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ProjectFactory.h"

namespace dew
{

/** The demos, as they are actually shipped.

    Reads them from the binary rather than rebuilding them from the factory, so
    what the Demos menu opens is exactly the file that was committed, rendered
    in CI and checked into examples/. A demo that only exists as code cannot
    drift from its file, but it also cannot be inspected, diffed or edited.
*/
struct DemoLibrary
{
    struct Entry
    {
        juce::String fileName;
        juce::String menuName;
        juce::String description;
    };

    static const std::vector<ProjectFactory::Demo>& entries() { return ProjectFactory::demos(); }

    /** The embedded bytes of one demo, or an empty string if it is not there. */
    static juce::String jsonFor (const juce::String& fileName);

    /** Loads a demo by index. An invalid index gives an invalid tree. */
    static juce::ValueTree load (int index, juce::StringArray& warnings);
};

} // namespace dew
