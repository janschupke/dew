#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace dew
{

/** Reads and writes the one-file .dew project format.

    The file is JSON so it is inspectable and diffable; the mapping itself lives
    in ProjectSchema, so this class only owns the envelope: the format tag, the
    version gate, and turning parse failures into messages a user can act on.
*/
struct ProjectSerializer
{
    struct LoadResult
    {
        juce::Result result = juce::Result::ok();
        juce::ValueTree tree;

        /** Recoverable problems: unknown keys, wrong types, missing properties
            that fell back to defaults. A file can load successfully and still
            have warnings.
        */
        juce::StringArray warnings;

        bool ok() const
        {
            return result.wasOk();
        }
    };

    static juce::String toJsonString (const juce::ValueTree& project);

    static LoadResult fromJsonString (const juce::String& json);

    static juce::Result writeToFile (const juce::ValueTree& project, const juce::File& file);

    static LoadResult readFromFile (const juce::File& file);
};

} // namespace dew
