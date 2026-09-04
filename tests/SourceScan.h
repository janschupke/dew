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

    Files under a path in `exempt` are not reported - that is the shape every
    one of these gates needs, because each rule has one or two places that are
    allowed to break it precisely because they are where it is defined.

    A PATH relative to src/, matching that file or anything beneath that
    directory: "ui/design/Tokens.h", "model/edits". It used to be a bare name,
    matched against a file name OR an immediate parent directory name, and that
    was wrong in both directions. Too wide, because any file anywhere called
    Tokens.h was exempt from six gates and nothing anchored it. Too narrow,
    because a name is not a property a file keeps: five gates went red during
    the work that split this tree, every one of them because code that was
    allowed to break a rule had moved to a file whose name was not on a list.

    And an exemption that suppresses NOTHING is reported as an offence of its
    own. An entry is a claim - this file breaks the rule because it is where the
    rule is defined - and ten of the twenty-one here were false. Tokens.h was
    exempt from the gate on drawn radii while calling nothing that draws, and
    from the gate on refresh rates while starting no timer. Ids.h was exempt
    from the gate on parameter names it spells with a macro, so they never
    appear as literals at all. Two of them - ProjectFactory.cpp and
    ProjectSchema.cpp - were a standing licence to write an undoable property by
    hand in exactly the two files that build the document.

    There is no second list for entries that are allowed to be idle. A list like
    that is the ratchet this is here to remove: when a file needs the exemption
    again, the commit that needs it puts it back.
*/
inline juce::StringArray offenders (const std::function<bool (const juce::String&)>& matches,
                                    std::initializer_list<const char*> exempt = {})
{
    juce::StringArray found;
    juce::Array<int> suppressed;

    suppressed.insertMultiple (0, 0, (int) exempt.size());

    for (const auto& file : sourceFiles())
    {
        const auto path = relativePathOf (file);

        // Every entry that covers this file, not just the first: two entries
        // that overlap would otherwise make one of them look idle.
        juce::Array<int> covering;
        auto index = 0;

        for (const auto* name : exempt)
        {
            if (path == name || path.startsWith (juce::String (name) + "/"))
                covering.add (index);

            ++index;
        }

        for (const auto& line : codeLinesWithNumbersOf (file))
        {
            if (! matches (line.text))
                continue;

            if (covering.isEmpty())
                found.add (path + ":" + juce::String (line.number) + "  " + line.text.trim());
            else
                for (const auto i : covering)
                    ++suppressed.getReference (i);
        }
    }

    auto index = 0;

    for (const auto* name : exempt)
    {
        if (suppressed[index] == 0)
            found.add (juce::String ("(exemption) ") + name
                       + "  hides nothing - either it was never needed, or the gate has "
                         "stopped being able to see what it is for");

        ++index;
    }

    return found;
}

} // namespace dew::testing
