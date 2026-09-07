#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "SourceScan.h"

using namespace dew::testing;

namespace
{

/** How many runs of two or more lower-case letters `text` holds. */
int lowerCaseRuns (const juce::String& text)
{
    auto runs = 0;
    auto length = 0;

    for (auto c : text)
    {
        if (c >= 'a' && c <= 'z')
        {
            if (++length == 2)
                ++runs;

            continue;
        }

        length = 0;
    }

    return runs;
}

/** True when a literal reads as a run of English words.

    A SHAPE, not a spelling. There is no word list here and no dictionary: what
    is asked is whether the characters look like prose, which is answered from
    character classes alone. That is what keeps the gate from having an opinion
    about "lowpass", "ff4fa3ff", "*.sf2;*.SF2" or a ValueTree identifier - dew's
    sources are full of literals that are not English, and a gate that cries
    wolf is one people turn off.

    Two lower-case runs rather than one is the whole of the first clause's
    accuracy. "filter" is one run and stays; "Add channel" is two and does not.

    The third clause is the one that pays for the design, and it is why this
    gate is per-LITERAL rather than per-call. `+` launders a fragment away from
    whatever sink it eventually reaches, so ", bar " and " steps" arrive at
    drawText as a variable and the sink gate beside this one cannot see them.
    A literal that begins or ends mid-phrase is a sentence being assembled -
    which i18n.md forbids for its own reasons - and it is exactly the shape the
    fragments of the three cursor announcements, the render summary and every
    engine warning had.

    The floor of two characters is measured rather than chosen: at three and
    four characters the tree held eight true hits (" step ", " bars", "Stem ")
    against one false (" bpm", a ParamSpec's SI suffix), and that one is
    stripped by its call shape below.
*/
bool isProse (const juce::String& literal)
{
    const auto trimmed = literal.trim();
    const auto runs = lowerCaseRuns (literal);

    if (literal.containsChar (' ') && runs >= 2)
        return true;

    if (literal.containsChar (' ') && runs >= 1
        && (trimmed.endsWithChar ('.') || trimmed.endsWithChar ('?') || trimmed.endsWithChar ('!')))
        return true;

    return runs >= 1 && trimmed.length() > 2
           && (literal.startsWithChar (' ') || literal.startsWith (", ")
               || literal.endsWithChar (' '));
}

/** Every quoted literal in `statement`, without its quotes.

    Character literals are skipped rather than read, and that is not a detail:
    src/lang/ScanCore.h holds `if (first == '"')`, so an extractor that does not
    know a char literal from a string one desynchronises there and reads the
    rest of the file inside out.
*/
juce::StringArray literalsIn (const juce::String& statement)
{
    juce::StringArray found;

    for (int i = 0; i < statement.length();)
    {
        const auto c = statement[i];

        if (c == '\'')
        {
            for (++i; i < statement.length(); ++i)
            {
                if (statement[i] == '\\')
                    ++i;
                else if (statement[i] == '\'')
                    break;
            }

            ++i;
            continue;
        }

        if (c != '"')
        {
            ++i;
            continue;
        }

        juce::String body;

        for (++i; i < statement.length(); ++i)
        {
            if (statement[i] == '\\' && i + 1 < statement.length())
            {
                body += statement.substring (i, i + 2);
                ++i;
                continue;
            }

            if (statement[i] == '"')
                break;

            body += statement[i];
        }

        found.add (body);
        ++i;
    }

    return found;
}

/** True when a statement's SHAPE says its literals are not prose.

    The same idiom as withoutArgumentNames and withoutDiagnosticCodes: a call
    whose argument is known not to be a sentence is stripped by what the call
    IS, rather than by which file it sits in. Every entry here is a class dew
    has already decided about, and i18n.md is where each decision is written
    down:

      - static_assert says something to a compiler, not to a person.
      - TransactionName is the name of an undo step, which only the tests read.
        It is a TYPE for this reason - see model/TransactionName.h.
      - beginNewTransaction is the same thing said through JUCE's own method,
        whose only argument is that name.
      - `case ...: return "` is a name table. nameOf (TokenKind) is one.
      - `{ &ids::` opens a ParamSpec row, whose suffix is an SI symbol.
      - a file name, a thread name and a log line are read by a filesystem, a
        debugger and whoever is working on dew.
      - shellCommand is a command typed into a terminal, and translating one
        would break it.
*/
bool shapeSaysItIsNotProse (const juce::String& statement)
{
    for (const auto* shape :
         { "static_assert (", "TransactionName", "beginNewTransaction (", "diagnostics::log (",
           "diagnostics::begin (", "getChildFile (", "juce::Thread (", "TimeSliceThread",
           "osxLibrarySubFolder", "{ &ids::", "shellCommand" })
        if (statement.contains (shape))
            return true;

    // A switch ARM, which is the shape rather than the spelling: a statement
    // that opens with `case` and answers with a literal is a name table.
    const auto trimmed = statement.trimStart();

    return trimmed.startsWith ("case ") && trimmed.contains ("return \"");
}

bool writesASentence (const juce::String& raw)
{
    const auto statement = withoutArgumentNames (raw);

    if (shapeSaysItIsNotProse (statement))
        return false;

    for (const auto& literal : literalsIn (statement))
        if (isProse (literal))
            return true;

    return false;
}

} // namespace

TEST_CASE ("no source writes a sentence a person reads", "[build][gate][i18n]")
{
    // The other half of "no source shows a person a string literal", and the
    // half that does not need to know where the sentence is going.
    //
    // That gate is a list of SINKS - setTooltip, addItem, drawText - and every
    // hole an audit of this tree found was structural rather than a missing
    // name. A helper laundered the literal into a variable before it reached a
    // listed sink (paint::emptyState drew ten sentences that way); a summary
    // was assigned to a member and drawn three hundred lines later; a
    // concatenation moved the fragments away from the call entirely, so
    // warnings.add - a LISTED sink - reported nothing at all because the
    // literal was never what the call started with. Sixty per cent of the
    // English left in the tree was invisible for one of those reasons.
    //
    // So this one asks the literal, not the call. It cannot see a one-word
    // label at a sink - "Rename", "Mixer" - which is exactly what the sink gate
    // is good at, and why both are here rather than one replacing the other.
    //
    // The exemptions are classes, not conveniences, and each is argued in
    // .ai/rules/i18n.md:
    //
    //   control            everything an agent reads over MCP is English by
    //                      contract - it is read by a model, and a tool's name
    //                      is a wire contract in the way an effect's id is.
    //   lang/Diagnostics   the clang/rustc output format itself, consumed by
    //                      dew_score and by tests rather than read as prose.
    //   lang/Completion    Completion::text is score syntax the compiler reads
    //                      back, and one completion is a phrase.
    //   io/LocalHttpServer HTTP's own furniture: header names and reason
    //                      phrases the protocol spells.
    //   app/Diagnostics    the crash header, written with write(2) from an
    //                      async-signal-safe handler. tr returns a reference
    //                      into a table and cannot be touched there at all.
    //   model/demos        a demo's content names are join keys - channelNamed
    //                      matches a score's channel against the project's BY
    //                      NAME - and the files are compared byte for byte
    //                      against what is committed.
    //   ScoreEditorLayout  the starter score's body, which is dew's own
    //                      language: a translated keyword would not compile.
    //                      Its four lines of English are score.starter.intro.
    //   design/DewGallery  not in the application; tools/shot_main.cpp renders
    //                      it for whoever is working on dew.
    const auto found = offenders (writesASentence,
                                  { "control", "lang/Diagnostics.cpp", "lang/Completion.cpp",
                                    "io/LocalHttpServer.cpp", "app/Diagnostics.cpp", "model/demos",
                                    "ui/ScoreEditorLayout.h", "ui/design/DewGallery.cpp" },
                                  statementsWithNumbersOf);

    INFO ("sentences written into the source:\n" << found.joinIntoString ("\n"));
    CHECK (found.isEmpty());
}

TEST_CASE ("the prose gate can see the defects it was written for", "[build][gate][i18n]")
{
    // A gate that cannot see its own subject is not a gate. Every line below
    // was in the tree when this was written.
    CHECK (writesASentence ("    summaryText = \"No audio device is open.\";"));
    CHECK (writesASentence ("        return \"The audio device is not running.\";"));
    CHECK (writesASentence ("    paint::emptyState (g, bounds, \"No channels. Use + Channel.\");"));
    CHECK (writesASentence ("    addTab (\"Channel Rack\", colour::background, &rack, false);"));

    // The fragment shape: what + leaves behind, and what no sink gate can see.
    CHECK (writesASentence ("    description += \", empty\";"));
    CHECK (writesASentence ("    d += name + \", bar \" + juce::String (bar);"));
    CHECK (writesASentence ("    parts.add (pattern[ids::lengthSteps].toString() + \" steps\");"));

    // And what it must not report. An id, a hex colour, a wildcard, a JSON key
    // and a one-word node type are all literals, and none of them is a
    // sentence.
    CHECK_FALSE (writesASentence ("    if (mode == \"lowpass\")"));
    CHECK_FALSE (writesASentence ("    chooser.browseForFileToOpen (\"*.sf2;*.SF2\");"));
    CHECK_FALSE (writesASentence ("    state->setProperty (\"oscillators\", slots);"));
    CHECK_FALSE (writesASentence ("    clip.setProperty (ids::kind, \"automation\", nullptr);"));
    CHECK_FALSE (writesASentence ("    return tr (StringId::status_notes, Args {}.count (n));"));

    // An argument name is an identifier spelled as a literal.
    CHECK_FALSE (writesASentence ("    tr (StringId::x, Args {}.with (\"first item\", n));"));

    // The stripped shapes, each of which is a class rather than a file.
    CHECK_FALSE (writesASentence ("    static_assert (a < b, \"the steps have to increase\");"));
    CHECK_FALSE (writesASentence ("    undo.beginNewTransaction (\"Add channel\");"));
    CHECK_FALSE (writesASentence (
        "    ProjectEdits::setProperty (n, p, v, &undo, TransactionName { \"Change pan\" });"));
    CHECK_FALSE (writesASentence ("        case TokenKind::endOfFile: return \"end of file\";"));
    CHECK_FALSE (writesASentence ("    { &ids::tempoBpm, \" bpm\", 20.0, 999.0, 128.0 },"));
    CHECK_FALSE (writesASentence ("    return folder.getChildFile (name + \" Assets\");"));

    // A char literal holding a quote, which is what src/lang/ScanCore.h does.
    CHECK_FALSE (writesASentence ("    if (first == '\"') return readText();"));
}
