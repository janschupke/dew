#include <juce_core/juce_core.h>

#include "BuildInfo.h"
#include "engine/OfflineRenderer.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

namespace
{

constexpr const char* usage = R"(dew_render - render a dew project to audio without an audio device

Usage:
  dew_render <project.dew> <output.wav> [options]
  dew_render --write-demo <project.dew>

Options:
  --seconds <n>      Render exactly n seconds (default: the length of the material)
  --pattern <id>     Render one pattern on a loop instead of the playlist
  --song             Render the playlist (default)
  --rate <hz>        Sample rate (default 44100)
  --version          Print build provenance and exit
  --help             Print this message

  --write-demo <f>   Write the built-in demo project to f and exit. Used to
                     regenerate examples/demo.dew.
)";

int fail (const juce::String& message)
{
    std::cerr << "dew_render: " << message << std::endl;
    return 1;
}

/** Command line, parsed so that both `--opt value` and `--opt=value` work.

    juce::ArgumentList deliberately supports only the `=` form for long options
    (getValueForOption returns the value after `=`, and takes the NEXT argument
    only for short options). Accepting `--seconds 4` silently as "no value" is
    exactly the kind of thing nobody notices until a render comes out the wrong
    length, so the parsing is done here instead.
*/
struct CommandLine
{
    explicit CommandLine (int argc, char* argv[])
    {
        juce::StringArray raw;

        for (int i = 1; i < argc; ++i)
            raw.add (juce::String::fromUTF8 (argv[i]));

        for (int i = 0; i < raw.size(); ++i)
        {
            const auto& arg = raw[i];

            if (! arg.startsWith ("-"))
            {
                positional.add (arg);
                continue;
            }

            const auto name = arg.upToFirstOccurrenceOf ("=", false, false);

            if (arg.contains ("="))
            {
                options.set (name, arg.fromFirstOccurrenceOf ("=", false, false));
            }
            else if (i + 1 < raw.size() && ! raw[i + 1].startsWith ("-"))
            {
                options.set (name, raw[i + 1]);
                ++i;
            }
            else
            {
                options.set (name, "");
            }
        }
    }

    bool has (const juce::String& name) const  { return options.containsKey (name); }

    juce::String value (const juce::String& name, const juce::String& fallback = {}) const
    {
        return has (name) && options[name].isNotEmpty() ? options[name] : fallback;
    }

    juce::StringPairArray options;
    juce::StringArray positional;
};

} // namespace

int main (int argc, char* argv[])
{
    const CommandLine args (argc, argv);

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

        const auto result = dew::ProjectSerializer::writeToFile (dew::ProjectFactory::createDemo(), target);

        if (result.failed())
            return fail (result.getErrorMessage());

        std::cout << "wrote " << target.getFullPathName() << std::endl;
        return 0;
    }

    const auto& positional = args.positional;

    if (positional.size() < 2)
    {
        std::cerr << usage << std::endl;
        return fail ("expected a project file and an output file");
    }

    const juce::File projectFile (juce::File::getCurrentWorkingDirectory()
                                      .getChildFile (positional[0]));
    const juce::File outputFile (juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (positional[1]));

    const auto loaded = dew::ProjectSerializer::readFromFile (projectFile);

    if (! loaded.ok())
        return fail (loaded.result.getErrorMessage());

    for (const auto& warning : loaded.warnings)
        std::cerr << "dew_render: warning: " << warning << std::endl;

    dew::RenderOptions options;

    if (args.has ("--seconds"))
        options.seconds = args.value ("--seconds").getDoubleValue();

    if (args.has ("--rate"))
        options.sampleRate = juce::jlimit (8000.0, 192000.0,
                                           args.value ("--rate", "44100").getDoubleValue());

    if (args.has ("--pattern"))
    {
        options.mode = dew::Transport::Mode::pattern;
        options.patternId = args.value ("--pattern", "1").getIntValue();
    }
    else
    {
        options.mode = dew::Transport::Mode::song;
    }

    const auto report = dew::OfflineRenderer::renderToFile (loaded.tree, outputFile, options);

    for (const auto& warning : report.warnings)
        std::cerr << "dew_render: warning: " << warning << std::endl;

    if (! report.ok())
        return fail (report.result.getErrorMessage());

    std::cout << "wrote " << outputFile.getFullPathName() << "\n"
              << "  " << juce::String (report.seconds, 2) << " s"
              << "  ·  " << report.numSamples << " frames"
              << "  ·  " << juce::String (options.sampleRate, 0) << " Hz"
              << "\n  peak " << juce::String (report.peak, 4)
              << "  ·  rms " << juce::String (report.rms, 4)
              << std::endl;

    // A render that produced silence is a failure, not a success with a quiet
    // file - it is the exact symptom of a project that loaded but did not play.
    if (report.peak <= 0.0f)
        return fail ("the render is silent");

    return 0;
}
