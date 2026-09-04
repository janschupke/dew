#include "control/OpsSupport.h"
#include "io/MidiExporter.h"
#include "io/RenderJob.h"

namespace dew::control
{

namespace
{

Transport::Mode modeFrom (const juce::String& text)
{
    return text == "pattern" ? Transport::Mode::pattern : Transport::Mode::song;
}

ControlResult renderAudio (ControlHost& host, const juce::var& args)
{
    auto* job = host.renderJob();

    if (job == nullptr)
        return ControlResult::failure ("this build has no render thread.");

    if (job->isRunning())
        return ControlResult::failure (
            "a render is already going. Call render_status, and wait for it.");

    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const juce::File destination { textArg (args, "path") };

    if (destination.getFullPathName().isEmpty())
        return ControlResult::failure ("render_audio needs a path.");

    if (! destination.getParentDirectory().isDirectory())
        return ControlResult::failure ("there is no folder at "
                                       + destination.getParentDirectory().getFullPathName() + ".");

    RenderJob::Request request;

    // Deep-copied by start(), because a ValueTree is not thread safe and the
    // person at the keyboard keeps editing while a render runs.
    request.project = project;
    request.destination = destination;
    request.stems = flagArg (args, "stems");

    // Both pools, always. A render given neither writes the synth parts alone
    // and reports success, which is the silent wrongness RenderOptions' own
    // comment exists to warn about.
    request.options.samplePool = host.samplePool();
    request.options.soundFontPool = host.soundFontPool();
    request.options.mode = modeFrom (textArg (args, "mode", "song"));
    request.options.patternId = intArg (args, "patternId", 1);
    request.options.seconds = numberArg (args, "seconds");

    if (! job->start (std::move (request), [] (const RenderReport&) {}))
        return ControlResult::failure ("the render could not be started.");

    return ControlResult::success (Obj {}
                                       .set ("started", true)
                                       .set ("path", destination.getFullPathName())
                                       .set ("stems", flagArg (args, "stems")));
}

ControlResult renderStatus (ControlHost& host, const juce::var&)
{
    auto* job = host.renderJob();

    if (job == nullptr)
        return ControlResult::failure ("this build has no render thread.");

    return ControlResult::success (Obj {}
                                       .set ("running", job->isRunning())
                                       .set ("progress", job->getProgress())
                                       .set ("stage", job->getStage()));
}

ControlResult exportMidi (ControlHost& host, const juce::var& args)
{
    const auto project = host.project();

    if (! project.isValid())
        return ControlResult::failure ("no project is open.");

    const juce::File destination { textArg (args, "path") };

    if (destination.getFullPathName().isEmpty())
        return ControlResult::failure ("export_midi needs a path.");

    if (! destination.getParentDirectory().isDirectory())
        return ControlResult::failure ("there is no folder at "
                                       + destination.getParentDirectory().getFullPathName() + ".");

    MidiExportOptions options;
    options.mode = modeFrom (textArg (args, "mode", "song"));
    options.patternId = intArg (args, "patternId", 1);

    // Synchronous, unlike an audio render, and legitimately so: writing a MIDI
    // file is arithmetic over the notes rather than a pass over every sample.
    const auto report = MidiExporter::writeToFile (project, destination, options);

    if (report.result.failed())
        return ControlResult::failure (report.result.getErrorMessage());

    juce::Array<juce::var> warnings;

    for (const auto& warning : report.warnings)
        warnings.add (warning);

    return ControlResult::success (Obj {}
                                       .set ("path", destination.getFullPathName())
                                       .set ("bytes", (int) destination.getSize())
                                       .set ("warnings", arrayOf (warnings)));
}

std::vector<ArgSpec> materialFields()
{
    return { { "mode", ValueKind::text, false,
               "song renders the arrangement, pattern renders one pattern. Song if absent." },
             { "patternId", ValueKind::integer, false, "Which pattern, in pattern mode." } };
}

} // namespace

void appendOutputOps (std::vector<OpSpec>& all)
{
    auto renderArgs = materialFields();
    renderArgs.insert (renderArgs.begin(),
                       { "path", ValueKind::text, true,
                         "Where to write. A file, or the folder to fill when stems is set." });
    renderArgs.push_back (
        { "stems", ValueKind::flag, false, "Write one file per channel instead of one mix." });
    renderArgs.push_back ({ "seconds", ValueKind::number, false,
                            "Render exactly this long, letting the material loop. 0, or absent, "
                            "renders it once." });

    auto midiArgs = materialFields();
    midiArgs.insert (midiArgs.begin(),
                     { "path", ValueKind::text, true, "Where to write the file." });

    all.push_back (
        { "render_audio", OpScope::write,
          "Start rendering the project to an audio file, or to one file per channel.",
          "Answers as soon as the render STARTS, because a render is seconds to minutes "
          "of work. Poll render_status for progress, and do not start a second one while "
          "one is going.\n\n"
          "The format follows the extension. MP3 needs the lame binary to be installed "
          "and reports itself unavailable when it is not.\n\n"
          "Stems mute rather than solo, so a stem carries the effects and the routing it "
          "has in the mix.",
          renderArgs, renderAudio });

    all.push_back (
        { "render_status",
          OpScope::read,
          "Report whether a render is going, and how far along it is.",
          "Progress runs 0 to 1 across the whole job, stems included. `stage` names which "
          "stem is being written.",
          {},
          renderStatus });

    all.push_back (
        { "export_midi", OpScope::write, "Write the project's notes to a MIDI file.",
          "Synchronous, unlike an audio render: writing MIDI is arithmetic over the "
          "notes rather than a pass over every sample.\n\n"
          "The metre's beatUnit goes into the file's time signature. Audio channels have "
          "no notes to write and are reported as warnings rather than silently dropped.",
          midiArgs, exportMidi });
}

} // namespace dew::control
