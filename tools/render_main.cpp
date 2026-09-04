#include <juce_core/juce_core.h>

#include "model/BuildInfo.h"
#include "io/OfflineRenderer.h"
#include "io/SamplePool.h"
#include "io/SoundFontPool.h"
#include "model/PresetFactory.h"
#include "model/PresetSerializer.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "CliArgs.h"

namespace
{

constexpr const char* usage = R"(dew_render - render a dew project to audio without an audio device

Usage:
  dew_render <project.dew> <output> [options]
  dew_render <project.dew> --stems <directory> [options]
  dew_render --write-demo <project.dew>

Scope:
  --seconds <n>      Render exactly n seconds (default: the length of the material)
  --pattern <id>     Render one pattern on a loop instead of the playlist
  --song             Render the playlist (default)
  --bars <a>:<b>     Render bars a to b, counting from 1, b exclusive of itself.
                     The earlier bars are still rendered, so tails and automation
                     arrive at bar a in the state playing there would leave them.

Format:
  --format <f>       wav (default), flac, mp3 or midi
  --rate <hz>        Sample rate (default 44100; mp3 takes 32000, 44100 or 48000)
  --bit-depth <n>    16 or 24 (default 24)
  --float            32-bit floating point instead, for wav
  --mp3-quality <i>  0-9 are VBR best to smallest, 10-23 are CBR 32 to 320 kb/s
  --no-dither        Leave 16-bit truncation undithered

Dynamics:
  --normalize        Scale so the file peaks at --peak
  --peak <db>        Where --normalize aims (default -1)
  --fade-in <s>      Ramp up over s seconds
  --fade-out <s>     Ramp down over s seconds
  --tail <s>         Time rendered after the material ends (default 1)

Other:
  --stems <dir>      One file per mixer track, into dir
  --keep-silent      Write stems for tracks nothing is routed to
  --version          Print build provenance and exit
  --help             Print this message

  --write-demo <f>   Write the built-in demo project to f and exit. Used to
                     regenerate examples/demo.dew.
  --write-demos <d>  Write the whole demo library into directory d and exit.
                     Used to regenerate examples/, which the app embeds.
  --write-presets <d> Write the factory presets into directory d and exit. Used
                     to regenerate presets/, which the app embeds.
)";

int fail (const juce::String& message)
{
    std::cerr << "dew_render: " << message << std::endl;
    return 1;
}

bool parseFormat (const juce::String& text, dew::RenderFormat& format)
{
    const auto lower = text.toLowerCase();

    if (lower == "wav")
    {
        format = dew::RenderFormat::wav;
        return true;
    }
    if (lower == "flac")
    {
        format = dew::RenderFormat::flac;
        return true;
    }
    if (lower == "mp3")
    {
        format = dew::RenderFormat::mp3;
        return true;
    }
    if (lower == "midi" || lower == "mid")
    {
        format = dew::RenderFormat::midi;
        return true;
    }

    return false;
}

/** "5:9" as bars counted from 1, into the half-open, 0-based range the renderer
    uses. Bar 1 is where the playhead starts, so the CLI counts the way the
    playlist's ruler does.
*/
bool parseBars (const juce::String& text, dew::BarRange& range)
{
    const auto separator = text.containsChar (':') ? ":" : "-";

    const auto first = text.upToFirstOccurrenceOf (separator, false, false).trim();
    const auto last = text.fromFirstOccurrenceOf (separator, false, false).trim();

    if (first.isEmpty() || last.isEmpty() || ! first.containsOnly ("0123456789")
        || ! last.containsOnly ("0123456789"))
        return false;

    range.firstBar = juce::jmax (0, first.getIntValue() - 1);
    range.lastBar = juce::jmax (range.firstBar + 1, last.getIntValue() - 1);

    return true;
}

/** The extension the chosen format wants, if the given path has none. */
juce::File withExtensionFor (const juce::File& file, dew::RenderFormat format)
{
    return file.getFileExtension().isEmpty()
               ? file.withFileExtension (dew::OfflineRenderer::extensionFor (format))
               : file;
}

} // namespace

int main (int argc, char* argv[])
{
    const dew::CliArgs args (argc, argv);

    if (args.has ("--help") || args.has ("-h")
        || (args.positional.isEmpty() && args.options.size() == 0))
    {
        std::cout << usage << std::endl;
        return 0;
    }

    if (args.has ("--version"))
    {
        std::cout << dew::BuildInfo::summary() << std::endl;
        return 0;
    }

    if (args.has ("--write-demo"))
    {
        const auto path = args.value ("--write-demo");

        if (path.isEmpty())
            return fail ("--write-demo needs a file to write to");

        const auto target = juce::File::getCurrentWorkingDirectory().getChildFile (path);

        const auto result = dew::ProjectSerializer::writeToFile (dew::ProjectFactory::createDemo(),
                                                                 target);

        if (result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << target.getFullPathName() << std::endl;
        return 0;
    }

    if (args.has ("--write-demos"))
    {
        const auto path = args.value ("--write-demos");

        if (path.isEmpty())
            return fail ("--write-demos needs a directory to write to");

        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile (path);

        if (! directory.createDirectory())
            return fail ("could not create " + directory.getFullPathName());

        for (const auto& demo : dew::ProjectFactory::demos())
        {
            const auto target = directory.getChildFile (demo.fileName);
            const auto result = dew::ProjectSerializer::writeToFile (demo.build(), target);

            if (result.failed())
                return fail (result.getErrorMessage());

            std::cout << "wrote " << target.getFullPathName() << std::endl;
        }

        return 0;
    }

    if (args.has ("--write-presets"))
    {
        const auto path = args.value ("--write-presets");

        if (path.isEmpty())
            return fail ("--write-presets needs a directory to write to");

        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile (path);

        if (! directory.createDirectory())
            return fail ("could not create " + directory.getFullPathName());

        for (const auto& entry : dew::PresetFactory::presets())
        {
            const auto target = directory.getChildFile (entry.fileName);
            const auto result = dew::PresetSerializer::writeToFile (
                dew::PresetFactory::buildFor (entry), target);

            if (result.failed())
                return fail (result.getErrorMessage());

            std::cout << "wrote " << target.getFullPathName() << std::endl;
        }

        return 0;
    }

    const auto& positional = args.positional;
    const auto renderingStems = args.has ("--stems");

    // With --stems the destination is the directory it names, so only the
    // project is positional.
    const auto neededPositional = renderingStems ? 1 : 2;

    if (positional.size() < neededPositional)
    {
        std::cerr << usage << std::endl;
        return fail (renderingStems ? "expected a project file"
                                    : "expected a project file and an output file");
    }

    const juce::File projectFile (
        juce::File::getCurrentWorkingDirectory().getChildFile (positional[0]));

    const auto loaded = dew::ProjectSerializer::readFromFile (projectFile);

    if (! loaded.ok())
        return fail (loaded.result.getErrorMessage());

    for (const auto& warning : loaded.warnings)
        std::cerr << "dew_render: warning: " << warning << std::endl;

    dew::RenderOptions options;

    // Audio channels read their samples through this, and relative paths are
    // relative to the project being rendered. Declared here so it outlives
    // every render below.
    dew::SamplePool samplePool;
    samplePool.setProjectFile (projectFile);
    options.samplePool = &samplePool;

    dew::SoundFontPool soundFontPool;
    soundFontPool.setProjectFile (projectFile);
    options.soundFontPool = &soundFontPool;

    // --- format ---------------------------------------------------------------
    if (args.has ("--format") && ! parseFormat (args.value ("--format"), options.format))
        return fail ("unknown format '" + args.value ("--format")
                     + "'; try wav, flac, mp3 or midi");

    if (args.has ("--rate"))
        options.sampleRate = juce::jlimit (8000.0, 192000.0,
                                           args.value ("--rate", "44100").getDoubleValue());

    if (args.has ("--bit-depth"))
        options.bitDepth = args.value ("--bit-depth", "24").getIntValue();

    if (args.has ("--float"))
    {
        options.floatingPoint = true;
        options.bitDepth = 32;
    }

    if (args.has ("--mp3-quality"))
        options.mp3QualityIndex = args.value ("--mp3-quality", "4").getIntValue();

    if (args.has ("--no-dither"))
        options.dither = false;

    // --- scope ----------------------------------------------------------------
    if (args.has ("--seconds"))
        options.seconds = args.value ("--seconds").getDoubleValue();

    if (args.has ("--tail"))
        options.tailSeconds = juce::jmax (0.0, args.value ("--tail", "1").getDoubleValue());

    if (args.has ("--bars") && ! parseBars (args.value ("--bars"), options.barRange))
        return fail ("--bars wants something like 5:9");

    if (args.has ("--pattern"))
    {
        options.mode = dew::Transport::Mode::pattern;
        options.patternId = args.value ("--pattern", "1").getIntValue();
    }
    else
    {
        options.mode = dew::Transport::Mode::song;
    }

    // --- dynamics -------------------------------------------------------------
    if (args.has ("--normalize"))
        options.normalize = true;

    if (args.has ("--peak"))
    {
        options.normalize = true;
        options.normalizePeakDb = (float) args.value ("--peak", "-1").getDoubleValue();
    }

    if (args.has ("--fade-in"))
        options.fadeInSeconds = juce::jmax (0.0, args.value ("--fade-in").getDoubleValue());

    if (args.has ("--fade-out"))
        options.fadeOutSeconds = juce::jmax (0.0, args.value ("--fade-out").getDoubleValue());

    if (args.has ("--keep-silent"))
        options.skipSilentStems = false;

    // --- go -------------------------------------------------------------------
    dew::RenderReport report;

    if (renderingStems)
    {
        const auto directory = args.value ("--stems");

        if (directory.isEmpty())
            return fail ("--stems needs a directory to write into");

        report = dew::OfflineRenderer::renderStems (
            loaded.tree, juce::File::getCurrentWorkingDirectory().getChildFile (directory),
            options);
    }
    else
    {
        const auto outputFile = withExtensionFor (
            juce::File::getCurrentWorkingDirectory().getChildFile (positional[1]), options.format);

        report = dew::OfflineRenderer::renderToFile (loaded.tree, outputFile, options);
    }

    for (const auto& warning : report.warnings)
        std::cerr << "dew_render: warning: " << warning << std::endl;

    if (! report.ok())
        return fail (report.result.getErrorMessage());

    for (const auto& file : report.files)
        std::cout << "wrote " << file.getFullPathName() << std::endl;

    if (options.format == dew::RenderFormat::midi)
    {
        std::cout << "  " << report.numSamples << " notes" << std::endl;
        return 0;
    }

    std::cout << "  " << juce::String (report.seconds, 2) << " s"
              << "  ·  " << report.numSamples << " frames"
              << "  ·  " << juce::String (options.sampleRate, 0) << " Hz"
              << "\n  peak " << juce::String (report.peak, 4) << "  ·  rms "
              << juce::String (report.rms, 4);

    if (report.normalizationGainDb != 0.0f)
        std::cout << "  ·  normalized " << juce::String (report.normalizationGainDb, 2) << " dB";

    std::cout << std::endl;

    // A render that produced silence is a failure, not a success with a quiet
    // file - it is the exact symptom of a project that loaded but did not play,
    // and of a --bars range that fell past the end of the material.
    //
    // The threshold is not zero. An effect tail decays towards zero without ever
    // reaching it, so a range long past the end of a project with a reverb on it
    // comes back at around 1e-30: silent by any measure that matters, and not
    // caught by a test for exactly zero. -120 dBFS is far below anything audible
    // and far above that residue.
    static constexpr float silenceThreshold = 1.0e-6f;

    if (report.peak <= silenceThreshold)
        return fail ("the render is silent");

    return 0;
}
