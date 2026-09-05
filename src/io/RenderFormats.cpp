// =============================================================================
// The file a render is written into: which format, whether it can honour the
// request, and how the bytes get there.
//
// Split out of OfflineRenderer.cpp, which is now about rendering AUDIO and
// touches no juce::AudioFormat at all.
//
// MP3 is why this is the larger half. JUCE can only decode it, so encoding
// drives an installed `lame` as a child process - which means finding lame,
// reporting the format unavailable when it is missing, and offering quality
// options that only apply to it.
// =============================================================================

#include "io/RenderFormats.h"

#include <memory>

#include "io/OfflineRenderer.h"
#include "engine/RenderPost.h"

namespace dew
{

namespace renderFormat
{

namespace
{

/** The audio format to write with, or nullptr if this machine cannot.

    Held by unique_ptr because LAMEEncoderAudioFormat has to be constructed with
    the path to the binary, so these cannot all be static instances.
*/
std::unique_ptr<juce::AudioFormat> audioFormatFor (const RenderOptions& options)
{
    switch (options.format)
    {
        case RenderFormat::wav: return std::make_unique<juce::WavAudioFormat>();

        case RenderFormat::flac: return std::make_unique<juce::FlacAudioFormat>();

        case RenderFormat::mp3:
        {
#if JUCE_USE_LAME_AUDIO_FORMAT
            const auto lame = options.lameExecutable != juce::File() ? options.lameExecutable
                                                                     : OfflineRenderer::findLame();

            if (! lame.existsAsFile())
                return {};

            return std::make_unique<juce::LAMEEncoderAudioFormat> (lame);
#else
            return {};
#endif
        }

        case RenderFormat::midi: break;
    }

    return {};
}

/** Checks the things the format cares about but its writer does not.

    LAMEEncoderAudioFormat publishes getPossibleSampleRates() and
    getPossibleBitDepths() and then enforces neither: ask it for 96kHz and it
    builds a writer, hands lame a file it cannot use, and fails silently in a
    destructor. So the checking happens here, where it can say something.
*/
} // namespace

juce::Result validateForFormat (const RenderOptions& options)
{
    if (options.format == RenderFormat::mp3)
    {
        if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
            return juce::Result::fail ("MP3 export needs the lame encoder, which was not found. "
                                       "Install it with `brew install lame`.");

        const auto rate = (int) std::llround (options.sampleRate);

        if (rate != 32000 && rate != 44100 && rate != 48000)
            return juce::Result::fail ("MP3 supports 32000, 44100 or 48000 Hz. This render is at "
                                       + juce::String (rate) + " Hz.");
    }

    if (options.format == RenderFormat::wav || options.format == RenderFormat::flac)
    {
        if (options.floatingPoint && options.bitDepth != 32)
            return juce::Result::fail ("A floating point file has to be 32-bit.");

        if (options.format == RenderFormat::flac && options.floatingPoint)
            return juce::Result::fail ("FLAC is an integer format; it cannot hold floats.");
    }

    return juce::Result::ok();
}

namespace
{

juce::AudioFormatWriterOptions writerOptionsFor (const RenderOptions& options)
{
    auto writerOptions = juce::AudioFormatWriterOptions()
                             .withSampleRate (options.sampleRate)
                             .withNumChannels (2);

    if (options.format == RenderFormat::mp3)
    {
        // 16 is the only depth lame's wrapper accepts, whatever was asked for.
        return writerOptions.withBitsPerSample (16).withQualityOptionIndex (
            juce::jlimit (0, juce::jmax (0, OfflineRenderer::mp3QualityOptions().size() - 1),
                          options.mp3QualityIndex));
    }

    writerOptions = writerOptions.withBitsPerSample (options.bitDepth);

    if (options.floatingPoint)
        writerOptions = writerOptions.withSampleFormat (
            juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    return writerOptions;
}

} // namespace

/** Dithers, then writes one buffer to one file, atomically.

    Shared by renderToFile and every stem, so the temporary-file swap, the
    format switch and the MP3 destructor rule exist in exactly one place.
*/
juce::Result writeAudio (juce::AudioBuffer<float>& buffer, const juce::File& destination,
                         const RenderOptions& options)
{
    // Dither belongs to the destination's LSB, so it happens here rather than in
    // renderToBuffer, which hands back a float buffer that has no bit depth. It
    // runs after the report was filled in, so peak and rms stay the numbers the
    // render produced rather than drifting by a fraction of an LSB.
    if (options.dither && ! options.floatingPoint)
        RenderPost::dither (buffer, options.bitDepth, options.ditherSeed);

    destination.getParentDirectory().createDirectory();

    // Write to a temporary and swap, as ProjectSerializer does. Writing in place
    // means a render that fails, or that the user cancels, destroys whatever
    // export was already sitting at that path.
    juce::TemporaryFile temp (destination);

    {
        auto fileStream = std::unique_ptr<juce::FileOutputStream> (
            temp.getFile().createOutputStream());

        if (fileStream == nullptr || ! fileStream->openedOk())
            return juce::Result::fail ("Could not create " + destination.getFullPathName());

        std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));

        auto format = audioFormatFor (options);

        if (format == nullptr)
            return juce::Result::fail ("Cannot write " + OfflineRenderer::nameFor (options.format)
                                       + " on this machine.");

        auto writer = format->createWriterFor (outputStream, writerOptionsFor (options));

        if (writer == nullptr)
            return juce::Result::fail ("Could not create a "
                                       + OfflineRenderer::nameFor (options.format) + " writer at "
                                       + juce::String (options.bitDepth) + "-bit / "
                                       + juce::String (options.sampleRate, 0) + " Hz.");

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
            return juce::Result::fail ("Could not write audio to " + destination.getFullPathName());
    }
    // The writer is destroyed HERE, and that brace is load-bearing. The MP3
    // writer streams to a temporary WAV and only runs lame when it is destroyed,
    // piping the result into the stream - so nothing exists until this point. It
    // also returns void and gives up silently after one retry, which is why the
    // size check below is required rather than defensive.

    if (temp.getFile().getSize() <= 0)
        return juce::Result::fail (OfflineRenderer::nameFor (options.format)
                                   + " encoding produced no output.");

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace " + destination.getFullPathName());

    return juce::Result::ok();
}

} // namespace renderFormat

juce::String OfflineRenderer::extensionFor (RenderFormat format) noexcept
{
    switch (format)
    {
        case RenderFormat::wav: return ".wav";
        case RenderFormat::flac: return ".flac";
        case RenderFormat::mp3: return ".mp3";
        case RenderFormat::midi: return ".mid";
    }

    return ".wav";
}

juce::String OfflineRenderer::nameFor (RenderFormat format) noexcept
{
    switch (format)
    {
        case RenderFormat::wav: return "WAV";
        case RenderFormat::flac: return "FLAC";
        case RenderFormat::mp3: return "MP3";
        case RenderFormat::midi: return "MIDI";
    }

    return "WAV";
}

juce::StringArray OfflineRenderer::mp3QualityOptions()
{
#if JUCE_USE_LAME_AUDIO_FORMAT
    const auto lame = findLame();

    if (lame.existsAsFile())
        return juce::LAMEEncoderAudioFormat (lame).getQualityOptions();
#endif

    return {};
}

juce::File OfflineRenderer::findLameIn (const juce::String& pathVariable)
{
    // Neither the separator nor the name is the same everywhere: PATH is
    // ':'-delimited on Unix and ';'-delimited on Windows, where the file is
    // lame.exe. Splitting on the wrong character does not find fewer
    // directories, it finds none - the whole variable becomes a single token.
#if JUCE_WINDOWS
    const auto* separator = ";";
    const auto* executable = "lame.exe";
#else
    const auto* separator = ":";
    const auto* executable = "lame";
#endif

    for (const auto& directory : juce::StringArray::fromTokens (pathVariable, separator, {}))
    {
        // A relative entry is legal in PATH and is not something juce::File
        // accepts: its constructor asserts on one. "." in a PATH is common
        // enough that stepping over it is cheaper than the assertion.
        if (directory.isEmpty() || ! juce::File::isAbsolutePath (directory))
            continue;

        if (const auto candidate = juce::File (directory).getChildFile (executable);
            candidate.existsAsFile())
            return candidate;
    }

    return {};
}

juce::File OfflineRenderer::findLame()
{
    // Cached: the answer cannot change while the app runs, and the UI asks
    // whenever it repaints a greyed-out menu entry.
    static const juce::File found = []
    {
        if (const auto onPath = findLameIn (juce::SystemStats::getEnvironmentVariable ("PATH", {}));
            onPath.existsAsFile())
            return onPath;

        // A GUI app launched from Finder does not inherit a shell's PATH, so the
        // usual Homebrew locations have to be named.
        for (const auto* fallback :
             { "/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/usr/bin/lame" })
            if (juce::File file { fallback }; file.existsAsFile())
                return file;

        return juce::File();
    }();

    return found;
}

bool OfflineRenderer::isAvailable (RenderFormat format)
{
    if (format != RenderFormat::mp3)
        return true;

#if JUCE_USE_LAME_AUDIO_FORMAT
    return findLame().existsAsFile();
#else
    return false;
#endif
}
} // namespace dew
