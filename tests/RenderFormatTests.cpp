#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "io/OfflineRenderer.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** A scratch path that cleans itself up, so a failing test does not leave
    half-written audio in the temp directory for the next run to trip over.
*/
struct ScratchFile
{
    explicit ScratchFile (const juce::String& extension)
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("dew-format-" + juce::Uuid().toString() + extension))
    {
    }

    ~ScratchFile() { file.deleteFile(); }

    juce::File file;
};

RenderOptions shortRender()
{
    RenderOptions options;
    options.seconds = 0.5;
    return options;
}

std::unique_ptr<juce::AudioFormatReader> readerFor (juce::AudioFormat& format, const juce::File& file)
{
    return std::unique_ptr<juce::AudioFormatReader> (
        format.createReaderFor (file.createInputStream().release(), true));
}

} // namespace

TEST_CASE ("a WAV is written at each integer bit depth", "[engine][render][format]")
{
    const auto depth = GENERATE (16, 24);

    ScratchFile scratch (".wav");

    auto options = shortRender();
    options.bitDepth = depth;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    INFO ("depth " << depth << ": " << report.result.getErrorMessage());
    REQUIRE (report.ok());

    juce::WavAudioFormat format;
    auto reader = readerFor (format, scratch.file);

    REQUIRE (reader != nullptr);
    REQUIRE ((int) reader->bitsPerSample == depth);
    REQUIRE (reader->numChannels == 2u);
    REQUIRE_FALSE (reader->usesFloatingPointData);
    REQUIRE ((int) reader->lengthInSamples == report.numSamples);
}

TEST_CASE ("a 32-bit float WAV says so, and is not clipped", "[engine][render][format]")
{
    ScratchFile scratch (".wav");

    auto options = shortRender();
    options.bitDepth = 32;
    options.floatingPoint = true;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());

    juce::WavAudioFormat format;
    auto reader = readerFor (format, scratch.file);

    REQUIRE (reader != nullptr);
    REQUIRE (reader->usesFloatingPointData);
    REQUIRE ((int) reader->bitsPerSample == 32);
}

TEST_CASE ("asking for float at anything but 32 bits is refused", "[engine][render][format]")
{
    ScratchFile scratch (".wav");

    auto options = shortRender();
    options.bitDepth = 24;
    options.floatingPoint = true;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    REQUIRE_FALSE (report.ok());
    REQUIRE_FALSE (scratch.file.existsAsFile());
}

TEST_CASE ("FLAC round-trips the render bit-exactly", "[engine][render][format]")
{
    ScratchFile scratch (".flac");

    auto options = shortRender();
    options.format = RenderFormat::flac;
    options.bitDepth = 24;
    options.dither = false;   // comparing against the float source, so no noise

    juce::AudioBuffer<float> expected;
    REQUIRE (OfflineRenderer::renderToBuffer (dew::testing::fixtureProject(), expected, options).ok());

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());

    juce::FlacAudioFormat format;
    auto reader = readerFor (format, scratch.file);

    REQUIRE (reader != nullptr);
    REQUIRE ((int) reader->lengthInSamples == expected.getNumSamples());

    juce::AudioBuffer<float> readBack (2, (int) reader->lengthInSamples);
    reader->read (&readBack, 0, (int) reader->lengthInSamples, 0, true, true);

    // Lossless means lossless: 24 bits is finer than the float mantissa needs
    // here, so every sample should survive the trip within one LSB.
    const auto lsb = 2.0f / (float) (1 << 24);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < expected.getNumSamples(); ++i)
            REQUIRE (std::abs (readBack.getSample (channel, i)
                               - expected.getSample (channel, i)) <= lsb);
}

TEST_CASE ("a format knows its own extension and name", "[render][format]")
{
    REQUIRE (OfflineRenderer::extensionFor (RenderFormat::wav) == ".wav");
    REQUIRE (OfflineRenderer::extensionFor (RenderFormat::flac) == ".flac");
    REQUIRE (OfflineRenderer::extensionFor (RenderFormat::mp3) == ".mp3");
    REQUIRE (OfflineRenderer::extensionFor (RenderFormat::midi) == ".mid");

    REQUIRE (OfflineRenderer::nameFor (RenderFormat::mp3) == "MP3");
}

TEST_CASE ("everything but mp3 is always available", "[render][format]")
{
    REQUIRE (OfflineRenderer::isAvailable (RenderFormat::wav));
    REQUIRE (OfflineRenderer::isAvailable (RenderFormat::flac));
    REQUIRE (OfflineRenderer::isAvailable (RenderFormat::midi));
}

TEST_CASE ("MP3 is refused clearly when lame is missing", "[engine][render][format][mp3]")
{
    if (OfflineRenderer::isAvailable (RenderFormat::mp3))
        SKIP ("lame is installed, so there is no missing-binary case to test");

    ScratchFile scratch (".mp3");

    auto options = shortRender();
    options.format = RenderFormat::mp3;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    REQUIRE_FALSE (report.ok());
    REQUIRE (report.result.getErrorMessage().contains ("lame"));
    REQUIRE_FALSE (scratch.file.existsAsFile());
}

TEST_CASE ("MP3 rejects a sample rate lame cannot take", "[engine][render][format][mp3]")
{
    if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
        SKIP ("lame is not installed");

    ScratchFile scratch (".mp3");

    auto options = shortRender();
    options.format = RenderFormat::mp3;
    options.sampleRate = 96000.0;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    // The writer would happily accept this and then fail inside a destructor.
    REQUIRE_FALSE (report.ok());
    REQUIRE (report.result.getErrorMessage().contains ("48000"));
}

TEST_CASE ("an MP3 is written and is recognisably one", "[engine][render][format][mp3]")
{
    if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
        SKIP ("lame is not installed");

    ScratchFile scratch (".mp3");

    auto options = shortRender();
    options.format = RenderFormat::mp3;

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());
    REQUIRE (scratch.file.getSize() > 0);

    juce::FileInputStream in (scratch.file);
    REQUIRE (in.openedOk());

    char header[3] = {};
    in.read (header, 3);

    // Either an ID3 tag or a raw MPEG frame sync.
    const auto isId3 = header[0] == 'I' && header[1] == 'D' && header[2] == '3';
    const auto isFrameSync = (juce::uint8) header[0] == 0xff
                             && ((juce::uint8) header[1] & 0xe0) == 0xe0;

    REQUIRE ((isId3 || isFrameSync));
}

TEST_CASE ("an out-of-range mp3 quality index cannot reach lame", "[engine][render][format][mp3]")
{
    if (! OfflineRenderer::isAvailable (RenderFormat::mp3))
        SKIP ("lame is not installed");

    ScratchFile scratch (".mp3");

    auto options = shortRender();
    options.format = RenderFormat::mp3;
    options.mp3QualityIndex = 9999;   // unclamped, this invokes lame with -b 0

    const auto report = OfflineRenderer::renderToFile (dew::testing::fixtureProject(),
                                                        scratch.file, options);

    INFO (report.result.getErrorMessage());
    REQUIRE (report.ok());
    REQUIRE (scratch.file.getSize() > 0);
}
