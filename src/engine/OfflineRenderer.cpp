#include "OfflineRenderer.h"

namespace dew
{

RenderReport OfflineRenderer::renderToBuffer (const juce::ValueTree& project,
                                                         juce::AudioBuffer<float>& destination,
                                                         const RenderOptions& options)
{
    RenderReport report;

    auto snapshot = buildSnapshot (project, &report.warnings);

    const auto patternIndex = snapshot.patternIndexForId (options.patternId);

    const auto loopSteps = Sequencer::loopLengthSteps (snapshot, options.mode, patternIndex);

    if (loopSteps <= 0)
    {
        report.result = juce::Result::fail (
            options.mode == Transport::Mode::song
                ? "This project has nothing in its playlist to render."
                : "Pattern " + juce::String (options.patternId) + " is empty or does not exist.");
        return report;
    }

    const auto samplesPerStep = Transport::samplesPerStepFor (snapshot.tempoBpm,
                                                              snapshot.stepsPerBeat,
                                                              options.sampleRate);

    const auto materialSamples = (juce::int64) std::llround (samplesPerStep * (double) loopSteps);

    const auto explicitLength = options.seconds > 0.0;

    auto totalSamples = explicitLength
                          ? (juce::int64) std::llround (options.seconds * options.sampleRate)
                          : materialSamples
                              + (juce::int64) std::llround (options.tailSeconds * options.sampleRate);

    totalSamples = juce::jmax ((juce::int64) 1, totalSamples);

    // Without an explicit length the material plays once and the rest is decay.
    // The transport loops, so the tail has to be produced by stopping it at the
    // end - otherwise the "tail" is the song starting over, which is both wrong
    // and loud.
    const auto stopAfterSamples = explicitLength ? totalSamples : materialSamples;

    AudioEngine engine;
    engine.prepare (options.sampleRate, options.blockSize);
    engine.publish (std::move (snapshot));
    engine.setMode (options.mode);
    engine.setCurrentPatternId (options.patternId);
    engine.rewind();
    engine.play();

    destination.setSize (2, (int) totalSamples);
    destination.clear();

    juce::AudioBuffer<float> block (2, options.blockSize);

    bool stopped = false;

    for (juce::int64 position = 0; position < totalSamples;)
    {
        if (! stopped && position >= stopAfterSamples)
        {
            // Voices keep rendering while stopped, so releases ring out.
            engine.stop();
            stopped = true;
        }

        const auto thisBlock = (int) juce::jmin ((juce::int64) options.blockSize,
                                                 totalSamples - position);

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, 0, thisBlock);
        engine.processBlock (view);

        for (int channel = 0; channel < 2; ++channel)
            destination.copyFrom (channel, (int) position, view, channel, 0, thisBlock);

        position += thisBlock;
    }

    report.numSamples = totalSamples;
    report.seconds = (double) totalSamples / options.sampleRate;
    report.peak = destination.getMagnitude (0, (int) totalSamples);
    report.rms = 0.5f * (destination.getRMSLevel (0, 0, (int) totalSamples)
                         + destination.getRMSLevel (1, 0, (int) totalSamples));

    return report;
}

RenderReport OfflineRenderer::renderToFile (const juce::ValueTree& project,
                                                       const juce::File& destination,
                                                       const RenderOptions& options)
{
    juce::AudioBuffer<float> rendered;
    auto report = renderToBuffer (project, rendered, options);

    if (! report.ok())
        return report;

    destination.getParentDirectory().createDirectory();
    destination.deleteFile();

    auto stream = std::unique_ptr<juce::FileOutputStream> (destination.createOutputStream());

    if (stream == nullptr)
    {
        report.result = juce::Result::fail ("Could not create " + destination.getFullPathName());
        return report;
    }

    juce::WavAudioFormat format;

    std::unique_ptr<juce::OutputStream> outputStream (std::move (stream));

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
        report.result = juce::Result::fail ("Could not write audio to " + destination.getFullPathName());

    return report;
}

} // namespace dew
