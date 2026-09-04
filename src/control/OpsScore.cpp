#include "control/OpsSupport.h"
#include "lang/Compile.h"
#include "lang/SourceRange.h"
#include "model/ScoreBake.h"

namespace dew::control
{

namespace
{

/** A compiler's diagnostics, as structured rows rather than a printed report.

    lang::CompileResult::report renders a caret diagram, which is right for a
    terminal and wrong here: a caller acting on an error wants the line and the
    column as numbers, and the code - E2xx name resolution, W6xx warning - which
    the language documents and keeps stable.
*/
juce::var diagnosticsOf (const lang::CompileResult& result, const juce::String& source)
{
    const auto utf8 = source.toStdString();
    const lang::LineIndex lines { utf8 };

    juce::Array<juce::var> rows;

    for (const auto& diagnostic : result.diagnostics)
    {
        Obj row;
        row.set ("severity", diagnostic.severity == lang::Severity::error ? "error" : "warning")
            .set ("code", juce::String (diagnostic.code))
            .set ("message", juce::String (diagnostic.message))
            .set ("line", lines.lineAt (diagnostic.primary.begin))
            .set ("column", lines.columnAt (diagnostic.primary.begin));

        juce::Array<juce::var> helps;

        for (const auto& help : diagnostic.helps)
            helps.add (juce::String (help));

        for (const auto& note : diagnostic.notes)
            helps.add (juce::String (note));

        rows.add (row.set ("help", arrayOf (helps)));
    }

    return arrayOf (rows);
}

ControlResult read (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto source = ProjectEdits::scoreSource (project);

    Obj answer;
    answer.set ("source", source)
        .set ("sourceName", ProjectEdits::scoreSourceName (project))
        .set ("lines", juce::StringArray::fromLines (source).size());

    // Checking is free and writes nothing, which is exactly what the editor
    // does as you type. Reporting it here means a caller can see whether the
    // stored source still compiles without asking for a compile that would put
    // notes on the undo stack.
    if (flagArg (args, "check", true) && source.isNotEmpty())
    {
        const auto result = lang::compile (source.toStdString(), "score");

        answer.set ("compiles", result.ok()).set ("diagnostics", diagnosticsOf (result, source));
    }

    return ControlResult::success (answer);
}

ControlResult write (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto source = textArg (args, "source");

    beginOneTransaction (host, "score_write");

    // Storing the text COMPILES NOTHING, deliberately. The editor saves as you
    // type and compiles when asked, because a compile writes notes and nobody
    // wants a pause in their typing to become an undo step full of them.
    ProjectEdits::setScoreSource (project, source, textArg (args, "sourceName", "score"),
                                  host.undoManager());

    const auto result = lang::compile (source.toStdString(), "score");

    return ControlResult::success (Obj {}
                                       .set ("stored", true)
                                       .set ("compiles", result.ok())
                                       .set ("diagnostics", diagnosticsOf (result, source)));
}

ControlResult compileInto (ControlHost& host, const juce::var& args)
{
    auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const auto source = hasArg (args, "source") ? textArg (args, "source")
                                                : ProjectEdits::scoreSource (project);

    if (source.isEmpty())
        return ControlResult::failure (
            "there is no score to compile. Write one with score_write first.");

    const auto result = lang::compile (source.toStdString(), "score");

    if (! result.ok() || ! result.score.has_value())
        return ControlResult::failure (
            "the score has " + juce::String (result.errorCount())
            + " error(s) and was not compiled. Call score_read for the diagnostics.");

    auto* undo = host.undoManager();
    beginOneTransaction (host, "score_compile");

    // Store the source in the same undo step as the notes it produced, when the
    // caller supplied one. A project whose baked notes and whose stored text
    // disagree is a project whose score cannot be recompiled to what is in it.
    if (hasArg (args, "source"))
        ProjectEdits::setScoreSource (project, source, textArg (args, "sourceName", "score"), undo);

    // keepHandEdits is the default and there is no separate rebake path: `into`
    // is idempotent, so recompiling is an UPDATE rather than a second copy, and
    // one code path is always the exercised one.
    const auto policy = flagArg (args, "discardHandEdits") ? ScoreBake::Policy::discardHandEdits
                                                           : ScoreBake::Policy::keepHandEdits;

    const auto report = ScoreBake::into (project, *result.score, undo, policy);

    host.flushEngine();

    juce::Array<juce::var> warnings;

    for (const auto& warning : report.warnings)
        warnings.add (warning);

    return ControlResult::success (Obj {}
                                       .set ("channelsCreated", report.channelsCreated)
                                       .set ("channelsAdopted", report.channelsAdopted)
                                       .set ("patternsWritten", report.patternsWritten)
                                       .set ("patternsKept", report.patternsKept)
                                       .set ("patternsRemoved", report.patternsRemoved)
                                       .set ("clipsWritten", report.clipsWritten)
                                       .set ("notesWritten", report.notesWritten)
                                       .set ("warnings", arrayOf (warnings))
                                       .set ("diagnostics", diagnosticsOf (result, source)));
}

} // namespace

void appendScoreOps (std::vector<OpSpec>& all)
{
    all.push_back ({ "score_read",
                     OpScope::read,
                     "Read the project's score source, and whether it still compiles.",
                     "A dew project can carry the text of the arrangement language that produced "
                     "it. Checking is free and writes nothing, so this reports the diagnostics "
                     "without putting a single note on the undo stack.\n\n"
                     "Read the dew://guide/score resource for the language itself.",
                     { { "check", ValueKind::flag, false,
                         "Compile it to report diagnostics. True if absent." } },
                     read });

    all.push_back (
        { "score_write",
          OpScope::write,
          "Store score source in the project, without compiling it.",
          "Storing and compiling are separate on purpose: a compile writes notes, and "
          "text that is being worked on should not fill the undo stack with them.\n\n"
          "The answer says whether what you stored compiles and why not, so you can fix "
          "it before asking for the notes.",
          { { "source", ValueKind::text, true, "The whole score, as text." },
            { "sourceName", ValueKind::text, false, "What to call it in diagnostics." } },
          write });

    all.push_back (
        { "score_compile",
          OpScope::write,
          "Compile the score into real patterns, notes and clips, as one undo step.",
          "This is the high-leverage way to arrange. The language says what the music IS "
          "- key, harmony, rhythm, voicing, counterpoint - and the compiler works out "
          "the notes, which is worth far more per call than placing them one at a time "
          "with notes_write.\n\n"
          "The language owns notes, patterns and clips; you own channels, instruments, "
          "effects and the mixer. Recompiling is an UPDATE rather than a second copy, "
          "and hand edits are kept unless you say otherwise.\n\n"
          "Refuses outright if the score has errors, rather than baking half of it. Note "
          "that the same source always compiles to the same notes, on any machine.",
          { { "source", ValueKind::text, false,
              "Compile this instead of what is stored, and store it too." },
            { "sourceName", ValueKind::text, false, "What to call it in diagnostics." },
            { "discardHandEdits", ValueKind::flag, false,
              "Overwrite notes edited by hand since the last compile." } },
          compileInto });
}

} // namespace dew::control
