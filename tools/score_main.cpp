// ==============================================================================
// dew_score - compiles a .score file into a .dew project.
//
// The language is verifiable headlessly here, the way playback is verifiable
// through dew_render: text in, project out, and then dew_render turns that into
// a WAV with no display and no audio device. CI runs both, so a regression in
// the compiler, in the bake, or in what the result sounds like fails the build.
// ==============================================================================

#include <juce_core/juce_core.h>

#include "CliArgs.h"
#include "lang/Compile.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSerializer.h"
#include "model/ScoreBake.h"

namespace
{

void printUsage()
{
    std::cout << R"(dew_score - compile a score into a dew project

  dew_score <in.score> --check              diagnostics only; exit 1 on any error
  dew_score <in.score> <out.dew>            compile and write a new project
  dew_score <in.score> <out.dew> --into <base.dew>
                                            bake into an existing project
  dew_score <in.score> --summary            what the score compiles to

  --discard-edits    replace generated patterns that have been edited by hand.
                     Without it they are kept, and counted in the summary.

The score is stored in the project it compiles to, so the .dew alone is enough
to recompile - and so the two can never be found out of step with each other.
Compiling twice writes the same document, not a second copy of everything.

Exits non-zero when the score does not compile, and when it compiles to no
notes at all - "parsed but produced nothing" is the failure this exists to
catch, the same way dew_render exits non-zero on a silent render.
)" << std::endl;
}

int fail (const juce::String& message)
{
    std::cerr << message << std::endl;
    return 1;
}

} // namespace

int main (int argc, char* argv[])
{
    const dew::CliArgs args { argc, argv };

    if (args.positional.isEmpty() || args.has ("--help") || args.has ("-h"))
    {
        printUsage();
        return args.positional.isEmpty() ? 1 : 0;
    }

    const juce::File source { juce::File::getCurrentWorkingDirectory().getChildFile (
        args.positional[0]) };

    if (! source.existsAsFile())
        return fail ("no such file: " + source.getFullPathName());

    const auto text = source.loadFileAsString().toStdString();
    const auto result = dew::lang::compile (text);

    // Diagnostics go to stderr so `--summary` and a redirect stay usable.
    if (! result.diagnostics.empty())
        std::cerr << result.report (text, source.getFileName().toStdString());

    if (! result.ok())
        return fail (juce::String (result.errorCount()) + " error(s); nothing written");

    const auto& score = *result.score;

    // "Compiled but produced nothing" is the failure worth catching: a score
    // that parses and yields silence looks like success from every other angle.
    if (score.noteCount() == 0)
        return fail ("the score compiled to no notes at all");

    if (args.has ("--check"))
    {
        std::cout << "ok: " << score.patterns.size() << " pattern(s), " << score.clips.size()
                  << " clip(s), " << score.noteCount() << " note(s)" << std::endl;
        return 0;
    }

    if (args.has ("--summary"))
    {
        std::cout << "title      " << score.title << "\n"
                  << "tempo      " << score.tempoBpm << " bpm\n"
                  << "meter      " << score.beatsPerBar << "/" << score.beatUnit << "\n"
                  << "grid       " << score.stepsPerBeat << " steps per beat\n"
                  << "bars       " << score.barsInSong << "\n"
                  << "channels   " << score.tracks.size() << "\n"
                  << "patterns   " << score.patterns.size() << "\n"
                  << "clips      " << score.clips.size() << "\n"
                  << "notes      " << score.noteCount() << " played, " << score.flatten().size()
                  << " after repeats" << std::endl;
        return 0;
    }

    if (args.positional.size() < 2)
    {
        printUsage();
        return fail ("nowhere to write - give an output file, --check or --summary");
    }

    const juce::File destination { juce::File::getCurrentWorkingDirectory().getChildFile (
        args.positional[1]) };

    dew::BakeReport report;
    juce::ValueTree project;

    const auto policy = args.has ("--discard-edits") ? dew::ScoreBake::Policy::discardHandEdits
                                                     : dew::ScoreBake::Policy::keepHandEdits;

    if (args.has ("--into"))
    {
        const juce::File base { juce::File::getCurrentWorkingDirectory().getChildFile (
            args.value ("--into")) };

        auto loaded = dew::ProjectSerializer::readFromFile (base);

        if (loaded.result.failed())
            return fail ("could not read " + base.getFullPathName() + ": "
                         + loaded.result.getErrorMessage());

        project = loaded.tree;
        report = dew::ScoreBake::into (project, score, nullptr, policy);
    }
    else
    {
        project = dew::ScoreBake::toNewProject (score, report);

        if (policy == dew::ScoreBake::Policy::discardHandEdits)
            std::cerr << "warning: --discard-edits does nothing without --into; "
                         "a new project has no edits to discard"
                      << std::endl;
    }

    // The source travels with what it compiled to. Without this a .dew is a
    // dead end - you can hear the score and never change it - and the two files
    // start drifting the moment either is copied without the other.
    dew::ProjectEdits::setScoreSource (project, source.loadFileAsString(), source.getFileName(),
                                       nullptr);

    for (const auto& warning : report.warnings)
        std::cerr << "warning: " << warning << std::endl;

    if (report.notesWritten == 0)
        return fail ("nothing was written to the project");

    if (const auto written = dew::ProjectSerializer::writeToFile (project, destination);
        written.failed())
        return fail ("could not write " + destination.getFullPathName() + ": "
                     + written.getErrorMessage());

    // Two different note counts, and saying which is which matters: the score
    // stores a pattern once and may place it several times, so "written" is
    // what is in the document and "played" is what you hear.
    std::cout << destination.getFullPathName() << "\n"
              << "  channels " << report.channelsCreated << " created, " << report.channelsAdopted
              << " adopted by name\n"
              << "  patterns " << report.patternsWritten << " written";

    if (report.patternsKept > 0)
        std::cout << ", " << report.patternsKept << " edited by hand and left alone";

    if (report.patternsRemoved > 0)
        std::cout << ", " << report.patternsRemoved << " removed";

    std::cout << "\n"
              << "  clips    " << report.clipsWritten << "\n"
              << "  notes    " << report.notesWritten << " written, " << score.noteCount()
              << " played" << std::endl;

    return 0;
}
