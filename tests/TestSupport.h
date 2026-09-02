#pragma once

#include <juce_core/juce_core.h>

namespace dew::testing
{

/** A directory that deletes itself.

    Written out three times, differing only in the prefix string - which is worth
    keeping, because a leaked directory is much easier to trace to its suite when
    it is named after it.
*/
struct TempDir
{
    explicit TempDir (const juce::String& prefix)
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile (prefix + juce::Uuid().toDashedString()))
    {
        dir.createDirectory();
    }

    ~TempDir() { dir.deleteRecursively(); }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    juce::File dir;
};

} // namespace dew::testing
