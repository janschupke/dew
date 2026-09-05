#pragma once

#include <juce_core/juce_core.h>

#include "model/Preset.h"

namespace dew
{

/** The value of a preset file's "format" key, so a wrong-but-valid JSON file is
    refused with a useful message instead of loading as a preset with no
    parameters. */
inline constexpr const char* kPresetFormatTag = "dew-preset";

/** The preset format's own version, deliberately NOT kFormatVersion.

    A preset is a different document from a project, and versioning them
    together would make a preset saved by a newer dew unreadable for reasons
    that have nothing to do with either format.

    2 added the category. A version 1 file still loads and simply arrives with
    no category - the reader refuses only a version it is too old to understand
    - so the bump records what the writer now emits rather than closing a door.
*/
inline constexpr int kPresetFormatVersion = 2;

/** Reads and writes one .dewpreset file.

    Shaped like ProjectSerializer and for the same reasons: JSON so it is
    inspectable and diffable, and this owns only the envelope - the format tag,
    the version gate, and which descriptor to read the payload with. The payload
    itself is validated by ModuleState against that descriptor, because its
    shape depends on the type and one NodeSpec cannot describe it.
*/
struct PresetSerializer
{
    struct LoadResult
    {
        juce::Result result = juce::Result::ok();
        Preset preset;
        juce::StringArray warnings;

        bool ok() const
        {
            return result.wasOk();
        }
    };

    static juce::String toJsonString (const Preset&);

    static LoadResult fromJsonString (const juce::String& json);

    static juce::Result writeToFile (const Preset&, const juce::File&);

    static LoadResult readFromFile (const juce::File&);
};

} // namespace dew
