#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** Samples in one bar, at the rate the options ask for. */
double samplesPerBar (const juce::ValueTree& project, const RenderOptions& options)
{
    const auto snapshot = buildSnapshot (project, nullptr);

    return Transport::samplesPerStepFor (snapshot.tempoBpm, snapshot.stepsPerBeat, options.sampleRate)
           * (double) snapshot.stepsPerBar();
}

} // namespace

TEST_CASE ("a bar range is sample-identical to the same window of a full render",
           "[engine][render][range]")
{
    const auto project = ProjectFactory::createDemo();

    RenderOptions whole;
    juce::AudioBuffer<float> full;
    REQUIRE (OfflineRenderer::renderToBuffer (project, full, whole).ok());

    RenderOptions ranged;
    ranged.barRange = { 1, 3 };
    juce::AudioBuffer<float> range;
    REQUIRE (OfflineRenderer::renderToBuffer (project, range, ranged).ok());

    const auto perBar = samplesPerBar (project, ranged);
    const auto offset = (int) std::llround (perBar);

    // Only the range itself. Past its end the ranged render is decaying, because
    // the transport stopped there, while the full render is still playing - which
    // is the entire point of asking for a range.
    const auto inRange = (int) std::llround (perBar * 2.0);

    REQUIRE (range.getNumSamples() > inRange);

    int mismatches = 0;

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < inRange; ++i)
            if (! juce::exactlyEqual (full.getSample (channel, offset + i),
                                      range.getSample (channel, i)))
                ++mismatches;

    // This is the assertion that rules out seeking to the in-point instead of
    // rendering up to it. A locate resets voices and leaves the effect units
    // empty, so it cannot produce these samples.
    INFO ("compared " << (inRange * 2) << " samples");
    REQUIRE (mismatches == 0);
}

TEST_CASE ("a bar range that starts mid-material is audible from its first sample",
           "[engine][render][range]")
{
    const auto project = ProjectFactory::createDemo();

    RenderOptions options;
    options.barRange = { 2, 3 };

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered, options);

    REQUIRE (report.ok());
    REQUIRE (rendered.getNumSamples() > 0);

    // A cold start would open on silence or on a dry, tail-less first block.
    const auto opening = rendered.getMagnitude (0, juce::jmin (2048, rendered.getNumSamples()));
    REQUIRE (opening > 0.0f);
}

TEST_CASE ("an empty bar range renders the whole material", "[engine][render][range]")
{
    const auto project = ProjectFactory::createDemo();

    juce::AudioBuffer<float> byDefault;
    const auto a = OfflineRenderer::renderToBuffer (project, byDefault, {});

    RenderOptions explicitlyEmpty;
    explicitlyEmpty.barRange = { 2, 2 };   // isEmpty(): lastBar <= firstBar

    juce::AudioBuffer<float> asked;
    const auto b = OfflineRenderer::renderToBuffer (project, asked, explicitlyEmpty);

    REQUIRE (a.ok());
    REQUIRE (b.ok());
    REQUIRE (a.numSamples == b.numSamples);
    REQUIRE (b.peak == Approx (a.peak));
}

TEST_CASE ("a bar range past the end of the material is silence, not the song again",
           "[engine][render][range]")
{
    const auto project = ProjectFactory::createDemo();

    RenderOptions options;
    options.barRange = { 16, 18 };
    options.tailSeconds = 0.0;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered, options);

    REQUIRE (report.ok());

    // The transport loops, so without the stop-at-the-material's-end rule this
    // would be the arrangement playing over again.
    REQUIRE (report.peak == Approx (0.0f).margin (1.0e-6f));
}

TEST_CASE ("a render reports progress and finishes at one", "[engine][render][progress]")
{
    RenderProgress progress;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(),
                                                         rendered, {}, &progress);

    REQUIRE (report.ok());
    REQUIRE_FALSE (report.cancelled);
    REQUIRE (progress.fraction.load() == Approx (1.0));
}

TEST_CASE ("a cancelled render stops, and is not an error", "[engine][render][progress]")
{
    RenderProgress progress;
    progress.cancelled.store (true);

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(),
                                                         rendered, {}, &progress);

    // Cancelling is something the user asked for. Reporting it as a failure
    // tells them their own click was a bug.
    REQUIRE (report.cancelled);
    REQUIRE (report.ok());
}

TEST_CASE ("a cancelled render leaves an existing file alone", "[engine][render][io]")
{
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("dew-cancel-" + juce::Uuid().toString() + ".wav");

    target.replaceWithText ("not audio, but it was here first");
    const auto before = target.loadFileAsString();

    RenderProgress progress;
    progress.cancelled.store (true);

    const auto report = OfflineRenderer::renderToFile (ProjectFactory::createDemo(),
                                                        target, {}, &progress);

    REQUIRE (report.cancelled);
    REQUIRE (target.existsAsFile());
    REQUIRE (target.loadFileAsString() == before);

    target.deleteFile();
}

TEST_CASE ("a completed render replaces the file at its destination", "[engine][render][io]")
{
    const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("dew-render-" + juce::Uuid().toString() + ".wav");

    target.replaceWithText ("stale");

    RenderOptions options;
    options.seconds = 0.25;

    const auto report = OfflineRenderer::renderToFile (ProjectFactory::createDemo(), target, options);

    REQUIRE (report.ok());
    REQUIRE (report.files.size() == 1);
    REQUIRE (report.files[0] == target);

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (
        format.createReaderFor (target.createInputStream().release(), true));

    REQUIRE (reader != nullptr);
    REQUIRE (reader->numChannels == 2u);
    REQUIRE (reader->sampleRate == Approx (options.sampleRate));
    REQUIRE ((int) reader->lengthInSamples == report.numSamples);

    reader.reset();
    target.deleteFile();
}
