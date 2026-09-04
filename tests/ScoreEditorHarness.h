#pragma once

#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "lang/Lexer.h"
#include "lang/ScanCore.h"
#include "ui/ScoreTokeniser.h"

/** The three sources the score tab's tests type into it, and the two ways of
    tokenising one.

    kindsThroughTheEditor and kindsThroughTheCompiler are the whole point of the
    completion tests: the editor highlights with juce::CodeDocument::Iterator
    and the compiler lexes bytes, and the two have to agree about what a token
    is or the squiggles land in the wrong place.
*/
namespace dew::testing
{

inline std::string exampleSource()
{
    const juce::File file { juce::String (DEW_EXAMPLES_DIR) + "/amber.score" };
    REQUIRE (file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

/** A short score that compiles, so a test can vary one line of it. */
inline std::string workingSource()
{
    return "song {\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "}\n"
           "channel pad { mixer 1 }\n"
           "voicing warm { size 3 voices }\n"
           "rhythm held { 1/1 }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n"
           "  length 4 bars\n"
           "  harmony h\n"
           "  part pad {\n"
           "    chords with warm\n"
           "    rhythm held\n"
           "  }\n"
           "}\n"
           "arrangement {\n  verse\n}\n";
}

/** A score needing a finer grid than a project starts with: a sixteenth needs
    four steps a beat and an eighth-note triplet needs three, so together they
    need twelve. amber.score, for all its length, only ever asks for four.
*/
inline std::string finerGridSource()
{
    return "song {\n"
           "  tempo 120\n"
           "  meter 4/4\n"
           "  key   C major\n"
           "}\n"
           "channel lead {\n  mixer 1\n  range C4..C6\n}\n"
           "rhythm swung { 1/16 1/16 1/8t 1/8t 1/8t }\n"
           "harmony h { I | vi | IV | V }\n"
           "section verse {\n  length 4 bars\n  harmony h\n"
           "  part lead {\n    melody {\n      rhythm swung\n    }\n  }\n}\n"
           "arrangement {\n  verse\n}\n";
}

/** The kinds the EDITOR's cursor produces, walking a CodeDocument. */
inline std::vector<lang::TokenKind> kindsThroughTheEditor (const juce::String& text)
{
    juce::CodeDocument document;
    document.replaceAllContent (text);

    juce::CodeDocument::Iterator iterator { document };
    std::vector<lang::TokenKind> kinds;

    while (! iterator.isEOF())
    {
        CodeDocumentCursor cursor { iterator };
        lang::skipSpace (cursor);

        if (cursor.isEOF())
            break;

        kinds.push_back (lang::scanOne (cursor));
    }

    return kinds;
}

/** The kinds the COMPILER produces, walking the same text as bytes. */
inline std::vector<lang::TokenKind> kindsThroughTheCompiler (const std::string& text)
{
    std::vector<lang::TokenKind> kinds;

    for (const auto& token : lang::tokenize (text))
        if (token.kind != lang::TokenKind::endOfFile)
            kinds.push_back (token.kind);

    return kinds;
}

} // namespace dew::testing
