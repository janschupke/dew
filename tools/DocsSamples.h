#pragma once

#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "lang/Lexer.h"
#include "ui/ScoreTokeniser.h"
#include "DocsJson.h"

namespace dew::docs
{

/** The colour role a token takes, as a name the web can use as a class.

    The values are ScoreTokeniser::Colour, which is what the EDITOR paints with,
    so a highlighted sample on the site and the same text in the Score tab
    cannot disagree.
*/
inline const char* roleName (int colour)
{
    // clang-format off
    switch (colour)
    {
        case ScoreTokeniser::plain:       return "plain";
        case ScoreTokeniser::keyword:     return "keyword";
        case ScoreTokeniser::literal:     return "literal";
        case ScoreTokeniser::stringText:  return "string";
        case ScoreTokeniser::comment:     return "comment";
        case ScoreTokeniser::punctuation: return "punctuation";
        case ScoreTokeniser::invalid:     return "invalid";
        default: break;
    }

    // clang-format on
    return "plain";
}

/** Example scores, with their text and the token runs that colour it.

    The website is a VIEWER, not a highlighter. .ai/rules/score-language.md is
    absolute that the highlighter is not a second grammar - lang::scanOne is a
    template and ScoreTokeniser is its second INSTANTIATION rather than a second
    implementation - and a TypeScript tokenizer would have been a third, held
    honest only by a test somebody has to keep believing in. Since the site
    renders committed samples and never user input, it does not need one: the
    runs are computed here, by the compiler's own scanner and the editor's own
    classifier, and the page paints spans.

    The source travels with the runs because the example scores live outside
    website/, and a page reading them across the folder boundary with fs would
    both break the static export and put the repo's layout inside a React
    component. One import, both jobs.

    Whitespace between tokens is deliberately not a run. The page emits the gap
    verbatim, which is what makes the rendered text byte-identical to `source`
    and lets a test assert exactly that.
*/
inline std::string samplesJson (const juce::Array<juce::File>& scores)
{
    JsonWriter json;
    json.beginArray();

    for (const auto& file : scores)
    {
        const auto text = file.loadFileAsString().toStdString();

        json.beginObject();
        json.key ("name");
        json.value (file.getFileNameWithoutExtension().toStdString());
        json.key ("source");
        json.value (text);

        json.key ("runs");
        json.beginArray();

        for (const auto& token : lang::tokenize (text))
        {
            if (token.kind == lang::TokenKind::endOfFile)
                continue;

            const auto lexeme = std::string (token.textIn (text));

            json.beginObject();
            json.key ("offset");
            json.value ((int) token.range.begin);
            json.key ("length");
            json.value ((int) token.range.length());
            json.key ("role");
            json.value (roleName (ScoreTokeniser::colourFor (token.kind, lexeme)));
            json.endObject();
        }

        json.endArray();
        json.endObject();
    }

    json.endArray();

    return json.str();
}

} // namespace dew::docs
