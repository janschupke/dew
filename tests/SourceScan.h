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

/** One line of code, and where in the file it came from.

    The number is the ORIGINAL line's, so a gate can strip the prose and still
    name a line somebody can go and look at.
*/
struct CodeLine
{
    int number = 0;    ///< 1-based, in the file as written
    juce::String text; ///< that line with its comments removed
};

/** A file's lines with comments removed and blank ones dropped.

    Every gate here wants this, and for the same reason. The lang gate strips
    comments because those files legitimately TALK about juce::CodeDocument; the
    size gate strips them because dew's headers carry long doc comments by design
    and a raw line count would punish exactly the documentation the house style
    asks for.

    Tracked across lines rather than per line, and that is the whole point.
    dew's doc comments continue WITHOUT a leading asterisk, so the per-line
    heuristic every predicate used to open with - does this line start with a
    comment marker - reads the body of every block comment as code. It was not a
    near miss: three gates were held green by a wrapped sentence rather than by
    any code, the combo-box gate's exempt line among them.

    STRINGS are tracked too, and that is not fussiness. This file's own gates
    quote comment markers as literals, and without this a line containing the
    characters slash-star inside quotes opened a block comment that ran until
    some later string closed it. SourceGateTests.cpp measured 301 code lines
    that way when it really held 667: the size gate was being blinded by the
    very predicates it was scanning, and the file it could not see was the one
    holding the gates.
*/
inline juce::Array<CodeLine> codeLinesWithNumbersOf (const juce::File& file)
{
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    juce::Array<CodeLine> code;
    auto inBlockComment = false;

    for (int i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        juce::String kept;

        for (int c = 0; c < line.length();)
        {
            if (inBlockComment)
            {
                if (line[c] == '*' && c + 1 < line.length() && line[c + 1] == '/')
                {
                    inBlockComment = false;
                    c += 2;
                }
                else
                {
                    ++c;
                }

                continue;
            }

            // A quoted literal is copied through whole, terminator included, so
            // the markers a gate quotes cannot open or close a comment.
            if (line[c] == '"' || line[c] == '\'')
            {
                const auto quote = line[c];
                kept += line[c++];

                while (c < line.length())
                {
                    const auto ch = line[c];
                    kept += ch;
                    ++c;

                    if (ch == '\\' && c < line.length())
                        kept += line[c++];
                    else if (ch == quote)
                        break;
                }

                continue;
            }

            if (line[c] == '/' && c + 1 < line.length())
            {
                if (line[c + 1] == '/')
                    break;

                if (line[c + 1] == '*')
                {
                    inBlockComment = true;
                    c += 2;
                    continue;
                }
            }

            kept += line[c++];
        }

        if (kept.trim().isNotEmpty())
            code.add ({ i + 1, kept });
    }

    return code;
}

/** The same, when only the count matters. */
inline juce::StringArray codeLinesOf (const juce::File& file)
{
    juce::StringArray text;

    for (const auto& line : codeLinesWithNumbersOf (file))
        text.add (line.text);

    return text;
}

/** Where a file sits under src/, as "ui/design/Tokens.h".

    The path rather than the name, because a name is not unique and nothing
    makes it so. Two gates compared offenders to files by base name and one of
    them - the engine gate - additionally asked only about the IMMEDIATE parent
    directory, which is how src/engine/modules stayed outside a gate about the
    engine layer for as long as that directory existed.
*/
inline juce::String relativePathOf (const juce::File& file)
{
    return file.getRelativePathFrom (juce::File { DEW_SOURCE_DIR }).replaceCharacter ('\\', '/');
}

/** Every line of CODE for which `matches` is true, as "ui/File.cpp:123  the line".

    Comments never reach `matches`, so a predicate says what it is looking for
    and nothing about how a comment is spelled.

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

        for (const auto& line : codeLinesWithNumbersOf (file))
            if (matches (line.text))
                found.add (relativePathOf (file) + ":" + juce::String (line.number) + "  "
                           + line.text.trim());
    }

    return found;
}

} // namespace dew::testing
