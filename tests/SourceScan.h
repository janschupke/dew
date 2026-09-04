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

/** Every .cpp and .h under DEW_TESTS_DIR and DEW_TOOLS_DIR as well.

    Only the size gate wants these. Every other gate is about a convention the
    application holds itself to, and a test is allowed to say juce::Font when it
    is checking that nothing else does.
*/
inline juce::Array<juce::File> allDewFiles()
{
    auto files = sourceFiles();

    for (const auto* root : { DEW_TESTS_DIR, DEW_TOOLS_DIR })
        for (const auto& entry :
             juce::RangedDirectoryIterator (juce::File { root }, true, "*.cpp;*.h"))
            files.add (entry.getFile());

    return files;
}

/** A file's lines with comments and blank lines removed.

    Hoisted out of the JUCE-in-lang gate, which had it inline: that gate strips
    comments because these files legitimately TALK about juce::CodeDocument, and
    the size gate strips them because dew's headers carry long doc comments by
    design and a raw line count would punish exactly the documentation the house
    style asks for.

    Tracked across lines rather than per line, because dew's doc comments
    continue WITHOUT a leading asterisk - so "starts with *" would read the body
    of every block comment as code.
*/
inline juce::StringArray codeLinesOf (const juce::File& file)
{
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    juce::StringArray code;
    auto inBlockComment = false;

    for (const auto& raw : lines)
    {
        auto line = raw;

        if (inBlockComment)
        {
            const auto closes = line.indexOf ("*/");

            if (closes < 0)
                continue;

            line = line.substring (closes + 2);
            inBlockComment = false;
        }

        if (const auto opens = line.indexOf ("/*"); opens >= 0)
        {
            if (const auto closes = line.indexOf (opens + 2, "*/"); closes < 0)
            {
                line = line.substring (0, opens);
                inBlockComment = true;
            }
            else
            {
                line = line.substring (0, opens) + line.substring (closes + 2);
            }
        }

        if (const auto lineComment = line.indexOf ("//"); lineComment >= 0)
            line = line.substring (0, lineComment);

        if (line.trim().isNotEmpty())
            code.add (line);
    }

    return code;
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
