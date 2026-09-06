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

/** Every .cpp and .h under DEW_TESTS_DIR, and nothing else.

    Separate from allDewFiles because a gate about the SUITE is a different
    question from a gate about the application: two helpers written out nine and
    sixteen times are a cost the tests pay, and no rule about src/ can see them.
*/
inline juce::Array<juce::File> testFiles()
{
    juce::Array<juce::File> files;

    for (const auto& entry :
         juce::RangedDirectoryIterator (juce::File { DEW_TESTS_DIR }, true, "*.cpp;*.h"))
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

/** `line` with every argument NAME removed.

    args ("value", rate) names the placeholder a message interpolates; it is an
    identifier that happens to be spelled as a literal, and no translator will
    ever see it. Without this the gate on English reports every formatted
    message as an offence, and the gate on a parameter written as a literal
    reports every message whose argument is named after one - `rate` and `depth`
    are both. The only way to quieten either would be a list of files, which is
    the ratchet the exemption rule exists to remove.

    Shared by those two gates, which is why it lives here rather than beside
    one of them.
*/
inline juce::String withoutArgumentNames (const juce::String& line)
{
    juce::String kept;
    auto i = 0;

    while (i < line.length())
    {
        const auto next = line.indexOf (i, ".with (\"");

        if (next < 0)
        {
            kept += line.substring (i);
            break;
        }

        const auto nameStart = next + 8;
        const auto nameEnd = line.indexOfChar (nameStart, '"');

        kept += line.substring (i, nameStart - 1);
        i = nameEnd < 0 ? line.length() : nameEnd + 1;
    }

    return kept;
}

/** A raw string's content, requoted so the rest of the scanner reads it as an
    ordinary literal.

    A raw string is the one place where text a person reads is not written as a
    quoted literal at all, and the scanner used to be blind to it in the worst
    possible direction: it tracked a string WITHIN a line, so `R"SCORE(` read as
    an unterminated one and every following line of the starter score was read
    as ordinary code - which deleted the score's own `//` lines as if they were
    C++ comments. The four sentences a person reads on opening the Score tab
    were invisible to every gate in this file.

    Requoting per PHYSICAL line rather than emitting one enormous literal is
    what keeps a line number pointing at the sentence that offends, and it keeps
    the statement joiner's parens balanced, because the delimiters themselves
    are consumed rather than kept.
*/
inline juce::String quotedRawLine (const juce::String& text)
{
    // A blank line inside a raw string is a blank line, and this reader drops
    // those - so it must not become an empty literal that reads as code.
    if (text.isEmpty())
        return {};

    auto quoted = juce::String::charToString ('"');
    quoted += text.replace ("\\", "\\\\").replace ("\"", "\\\"");
    quoted += '"';
    return quoted;
}

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

    RAW strings are tracked across lines as well, through quotedRawLine above.
    They are the one shape where a sentence a person reads is not written as a
    quoted literal, and reading them as code is worse than reading them as
    nothing: a raw string holding `//` had those lines deleted as C++ comments,
    and one holding slash-star would open a comment that swallowed the rest of
    the file - the same failure the paragraph above records, one level down.
*/
inline juce::Array<CodeLine> codeLinesWithNumbersOf (const juce::File& file)
{
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    juce::Array<CodeLine> code;
    auto inBlockComment = false;
    juce::String rawTerminator;

    for (int i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        juce::String kept;

        for (int c = 0; c < line.length();)
        {
            // Inside a raw string, everything up to its own delimiter is text.
            if (rawTerminator.isNotEmpty())
            {
                const auto end = line.indexOf (c, rawTerminator);

                if (end < 0)
                {
                    kept += quotedRawLine (line.substring (c));
                    c = line.length();
                    continue;
                }

                kept += quotedRawLine (line.substring (c, end));
                c = end + rawTerminator.length();
                rawTerminator.clear();
                continue;
            }

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

            // R"delim( ... )delim" - the opening, which has to be recognised
            // BEFORE the quote handler below or the R is copied as code and the
            // quote after it opens a literal that never closes.
            if (line[c] == 'R' && c + 1 < line.length() && line[c + 1] == '"'
                && (c == 0 || ! juce::CharacterFunctions::isLetterOrDigit (line[c - 1])))
            {
                if (const auto open = line.indexOfChar (c + 2, '('); open > 0)
                {
                    rawTerminator = juce::String::charToString (')');
                    rawTerminator += line.substring (c + 2, open);
                    rawTerminator += '"';
                    c = open + 1;
                    continue;
                }
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

/** A file's CODE as logical STATEMENTS, each numbered by the line it starts on.

    The gate on English written into a source used to read physical lines, and
    `.clang-format` is what made that wrong. ColumnLimit is 100, so the longest
    arguments in the tree - which are exactly the sentences a person reads - get
    wrapped onto a line of their own:

        ditherToggle.setTooltip (
            "Adds inaudible noise so 16-bit truncation does not distort quiet passages");

    A line-oriented predicate sees `setTooltip (` with nothing after it, and
    passes. The formatter the repository REQUIRES was hiding offences from the
    gate, and the five it was hiding were all of this shape: the longer the
    sentence, the more likely it wrapped, so the gate was blindest to exactly
    the strings that mattered most.

    Joined on PAREN depth only, deliberately. Tracking braces too would swallow
    a whole function body into one "statement", because a function's opening
    brace is indistinguishable from a brace initialiser's here. So a constructor
    argument split across lines is still invisible - the sinks that read braces
    say so where they are written - and every call-shaped sink is covered.
*/
inline juce::Array<CodeLine> statementsWithNumbersOf (const juce::File& file)
{
    juce::Array<CodeLine> statements;

    CodeLine building;
    auto depth = 0;

    for (const auto& line : codeLinesWithNumbersOf (file))
    {
        if (building.number == 0)
            building.number = line.number;

        building.text += (building.text.isEmpty() ? "" : " ") + line.text.trim();

        // Quotes are still here - only comments were stripped - so a paren
        // inside a string must not count. An escaped quote does not close one.
        auto inString = false;

        for (int i = 0; i < line.text.length(); ++i)
        {
            const auto c = line.text[i];

            if (inString)
            {
                if (c == '\\')
                    ++i;
                else if (c == '"')
                    inString = false;

                continue;
            }

            if (c == '"')
                inString = true;
            else if (c == '(')
                ++depth;
            else if (c == ')')
                depth = juce::jmax (0, depth - 1);
        }

        // A line ending in `{` opens a BLOCK - a lambda body passed as an
        // argument is the common one - and a block is not a continuation of
        // the call that carries it. Without this the join runs to the closing
        // paren of the outermost call and swallows a whole resolver lambda,
        // which then reports every literal inside it against whichever sink
        // happened to appear first. A wrapped argument list never ends a line
        // with a brace, so nothing this gate is for is lost.
        if (depth > 0 && ! building.text.trimEnd().endsWith ("{"))
            continue;

        statements.add (building);
        building = {};
        depth = 0;
    }

    if (building.text.isNotEmpty())
        statements.add (building);

    return statements;
}

/** Where a file sits under src/, as "ui/design/Tokens.h".

    The path rather than the name, because a name is not unique and nothing
    makes it so. Two gates compared offenders to files by base name and one of
    them - the engine gate - additionally asked only about the IMMEDIATE parent
    directory, which is how src/engine/modules stayed outside a gate about the
    engine layer for as long as that directory existed.
*/
inline juce::String relativePathOf (const juce::File& file, const juce::File& root)
{
    return file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

inline juce::String relativePathOf (const juce::File& file)
{
    return relativePathOf (file, juce::File { DEW_SOURCE_DIR });
}

/** How a file is cut up for a predicate: physical lines, or logical statements.

    A parameter rather than a second copy of offenders(), because everything
    below it - the exemption bookkeeping, and the rule that an exemption hiding
    nothing is itself an offence - is the part worth having once.
*/
using LineReader = juce::Array<CodeLine> (*) (const juce::File&);

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
inline juce::StringArray offendersIn (const juce::Array<juce::File>& files, const juce::File& root,
                                      const std::function<bool (const juce::String&)>& matches,
                                      std::initializer_list<const char*> exempt = {},
                                      LineReader read = codeLinesWithNumbersOf)
{
    juce::StringArray found;
    juce::Array<int> suppressed;

    suppressed.insertMultiple (0, 0, (int) exempt.size());

    for (const auto& file : files)
    {
        const auto path = relativePathOf (file, root);

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

        for (const auto& line : read (file))
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

/** The same over src/, which is what every gate about the application asks.

    A wrapper rather than the other way round: paths are relative to src/ here,
    so an exemption reads "ui/design/Tokens.h" and a gate over the suite reads
    "TestSupport.h" without either having to say which tree it means.
*/
inline juce::StringArray offenders (const std::function<bool (const juce::String&)>& matches,
                                    std::initializer_list<const char*> exempt = {},
                                    LineReader read = codeLinesWithNumbersOf)
{
    return offendersIn (sourceFiles(), juce::File { DEW_SOURCE_DIR }, matches, exempt, read);
}

} // namespace dew::testing
