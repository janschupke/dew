#pragma once

#include <functional>
#include <initializer_list>

#include <juce_core/juce_core.h>

/** Reading dew's own source, so a convention can be a test rather than a habit.

    The typography gate proved the idea: "no source file constructs a font
    outside the design system" is enforced by scanning every .cpp and .h, and it
    names the file and line when it fails. This is that machinery, factored out
    so the next such rule costs three lines instead of thirty.
*/
namespace dew::testing
{

/** Every .cpp and .h under DEW_SOURCE_DIR.

    DEW_SOURCE_DIR is ${CMAKE_SOURCE_DIR}/src, set in tests/CMakeLists.txt. If
    production code ever moves OUT of src/, every gate built on this silently
    stops covering it while staying green - which is why there is a test that
    checks this walk against what is actually compiled.
*/
inline juce::Array<juce::File> sourceFiles()
{
    juce::Array<juce::File> files;

    const juce::File root { DEW_SOURCE_DIR };

    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.cpp;*.h"))
        files.add (entry.getFile());

    return files;
}

/** Every line for which `matches` is true, as "File.cpp:123  the line".

    Files whose NAME appears in `exempt` are skipped whole - that is the shape
    every one of these gates needs, because each rule has one or two places that
    are allowed to break it precisely because they are where it is defined.
*/
inline juce::StringArray offenders (const std::function<bool (const juce::String&)>& matches,
                                    std::initializer_list<const char*> exempt = {})
{
    juce::StringArray found;

    for (const auto& file : sourceFiles())
    {
        auto skip = false;

        // A name matches a FILE or a DIRECTORY. The directory form exists
        // because src/lang is a different language with its own vocabulary: its
        // keywords coinciding with dew's property names is a coincidence, and
        // exempting five of its files by name would be a list that the sixth
        // silently escaped.
        for (const auto* name : exempt)
            if (file.getFileName() == name || file.getParentDirectory().getFileName() == name)
                skip = true;

        if (skip)
            continue;

        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        for (int i = 0; i < lines.size(); ++i)
            if (matches (lines[i]))
                found.add (file.getFileName() + ":" + juce::String (i + 1) + "  "
                           + lines[i].trim());
    }

    return found;
}

} // namespace dew::testing
