#include "OfflineRenderer.h"

namespace dew
{

namespace
{

/** Which samples to render, and which of them to keep.

    `firstSample` is where the KEPT audio starts; everything before it is
    rendered and discarded so that the engine arrives at the in-point in the
    state it would have been in on the way past. See the note on OfflineRenderer
    for why that is not negotiable.
*/
struct RenderSpan
{
    juce::int64 firstSample = 0;    ///< first sample kept
    juce::int64 totalSamples = 0;   ///< samples rendered, counted from zero
    juce::int64 stopAfterSamples = 0;
    juce::int64 numKept() const noexcept { return juce::jmax ((juce::int64) 0, totalSamples - firstSample); }
};

juce::Result planSpan (const EngineSnapshot& snapshot,
                       const RenderOptions& options,
                       int patternIndex,
                       RenderSpan& span)
{
    const auto loopSteps = Sequencer::loopLengthSteps (snapshot, options.mode, patternIndex);

    if (loopSteps <= 0)
        return juce::Result::fail (
            options.mode == Transport::Mode::song
                ? "This project has nothing in its playlist to render."
                : "Pattern " + juce::String (options.patternId) + " is empty or does not exist.");

    const auto samplesPerStep = Transport::samplesPerStepFor (snapshot.tempoBpm,
                                                              snapshot.stepsPerBeat,
                                                              options.sampleRate);

    const auto materialSamples = (juce::int64) std::llround (samplesPerStep * (double) loopSteps);
    const auto tailSamples = (juce::int64) std::llround (options.tailSeconds * options.sampleRate);

    if (! options.barRange.isEmpty())
    {
        // A range is expressed in bars, so it is the one scope whose length does
        // not depend on how long the material happens to be.
        const auto samplesPerBar = samplesPerStep * (double) snapshot.stepsPerBar();

        span.firstSample = (juce::int64) std::llround ((double) options.barRange.firstBar * samplesPerBar);
        const auto lastSample = (juce::int64) std::llround ((double) options.barRange.lastBar * samplesPerBar);

        span.totalSamples = lastSample + tailSamples;

        // Never let the transport wrap back to the top inside a range: past the
        // end of the material the range is silence, not the song again.
        span.stopAfterSamples = juce::jmin (lastSample, materialSamples);
    }
    else if (options.seconds > 0.0)
    {
        // An explicit length renders exactly that long and lets the material loop.
        span.firstSample = 0;
        span.totalSamples = (juce::int64) std::llround (options.seconds * options.sampleRate);
        span.stopAfterSamples = span.totalSamples;
    }
    else
    {
        // The material plays once and the rest is decay. The transport loops, so
        // the tail has to be produced by stopping it at the end - otherwise the
        // "tail" is the song starting over, which is both wrong and loud.
        span.firstSample = 0;
        span.totalSamples = materialSamples + tailSamples;
        span.stopAfterSamples = materialSamples;
    }

    span.totalSamples = juce::jmax ((juce::int64) 1, span.totalSamples);
    span.firstSample = juce::jlimit ((juce::int64) 0, span.totalSamples - 1, span.firstSample);

    return juce::Result::ok();
}

/** Renders one already-built snapshot through one already-prepared engine.

    Split out of renderToBuffer so that stems can drive it N times over the same
    engine: constructing an AudioEngine per stem churns thirty-two preallocated
    effect units for nothing.

    `progressFrom`/`progressTo` are this pass's slice of the whole job, so N
    stems report one continuous 0..1 rather than N sawtooths.
*/
RenderReport renderSnapshot (EngineSnapshot snapshot,
                             AudioEngine& engine,
                             juce::AudioBuffer<float>& destination,
                             const RenderOptions& options,
                             RenderProgress* progress,
                             double progressFrom,
                             double progressTo)
{
    RenderReport report;

    const auto patternIndex = snapshot.patternIndexForId (options.patternId);

    RenderSpan span;
    report.result = planSpan (snapshot, options, patternIndex, span);

    if (report.result.failed())
        return report;

    engine.prepare (options.sampleRate, options.blockSize);
    engine.publish (std::move (snapshot));
    engine.setMode (options.mode);
    engine.setCurrentPatternId (options.patternId);
    engine.rewind();
    engine.play();

    destination.setSize (2, (int) span.numKept());
    destination.clear();

    juce::AudioBuffer<float> block (2, options.blockSize);

    bool stopped = false;

    for (juce::int64 position = 0; position < span.totalSamples;)
    {
        if (progress != nullptr)
        {
            if (progress->cancelled.load (std::memory_order_relaxed))
            {
                report.cancelled = true;
                return report;
            }

            const auto fraction = (double) position / (double) span.totalSamples;
            progress->fraction.store (progressFrom + (progressTo - progressFrom) * fraction,
                                      std::memory_order_relaxed);
        }

        if (! stopped && position >= span.stopAfterSamples)
        {
            // Voices keep rendering while stopped, so releases ring out.
            engine.stop();
            stopped = true;
        }

        const auto thisBlock = (int) juce::jmin ((juce::int64) options.blockSize,
                                                 span.totalSamples - position);

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, 0, thisBlock);
        engine.processBlock (view);

        // Blocks before the in-point are rendered for their effect on the
        // engine's state and then thrown away.
        const auto keepFrom = juce::jmax (position, span.firstSample);
        const auto keepTo = juce::jmin (position + thisBlock, span.totalSamples);

        if (keepTo > keepFrom)
            for (int channel = 0; channel < 2; ++channel)
                destination.copyFrom (channel,
                                      (int) (keepFrom - span.firstSample),
                                      view,
                                      channel,
                                      (int) (keepFrom - position),
                                      (int) (keepTo - keepFrom));

        position += thisBlock;
    }

    if (progress != nullptr)
        progress->fraction.store (progressTo, std::memory_order_relaxed);

    const auto kept = (int) span.numKept();

    report.numSamples = kept;
    report.seconds = (double) kept / options.sampleRate;
    report.peak = destination.getMagnitude (0, kept);
    report.rms = 0.5f * (destination.getRMSLevel (0, 0, kept)
                         + destination.getRMSLevel (1, 0, kept));

    return report;
}

} // namespace

RenderReport OfflineRenderer::renderToBuffer (const juce::ValueTree& project,
                                              juce::AudioBuffer<float>& destination,
                                              const RenderOptions& options,
                                              RenderProgress* progress)
{
    juce::StringArray warnings;
    auto snapshot = buildSnapshot (project, &warnings);

    AudioEngine engine;
    auto report = renderSnapshot (std::move (snapshot), engine, destination, options,
                                  progress, 0.0, 1.0);

    report.warnings.addArray (warnings);
    return report;
}

RenderReport OfflineRenderer::renderToFile (const juce::ValueTree& project,
                                            const juce::File& destination,
                                            const RenderOptions& options,
                                            RenderProgress* progress)
{
    juce::AudioBuffer<float> rendered;
    auto report = renderToBuffer (project, rendered, options, progress);

    if (! report.ok() || report.cancelled)
        return report;

    destination.getParentDirectory().createDirectory();

    // Write to a temporary and swap, as ProjectSerializer does. Writing in place
    // means a render that fails, or that the user cancels, destroys whatever
    // export was already sitting at that path.
    juce::TemporaryFile temp (destination);

    {
        auto fileStream = std::unique_ptr<juce::FileOutputStream> (temp.getFile().createOutputStream());

        if (fileStream == nullptr || ! fileStream->openedOk())
        {
            report.result = juce::Result::fail ("Could not create " + destination.getFullPathName());
            return report;
        }

        std::unique_ptr<juce::OutputStream> outputStream (std::move (fileStream));

        juce::WavAudioFormat format;

        auto writer = format.createWriterFor (outputStream,
                                              juce::AudioFormatWriterOptions()
                                                  .withSampleRate (options.sampleRate)
                                                  .withNumChannels (2)
                                                  .withBitsPerSample (options.bitDepth));

        if (writer == nullptr)
        {
            report.result = juce::Result::fail ("Could not create a WAV writer.");
            return report;
        }

        if (! writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples()))
        {
            report.result = juce::Result::fail ("Could not write audio to " + destination.getFullPathName());
            return report;
        }
    }

    if (! temp.overwriteTargetFileWithTemporary())
    {
        report.result = juce::Result::fail ("Could not replace " + destination.getFullPathName());
        return report;
    }

    report.files.add (destination);
    return report;
}

} // namespace dew
