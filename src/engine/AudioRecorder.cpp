#include "AudioRecorder.h"

namespace dew
{

AudioRecorder::AudioRecorder()
{
    backgroundThread.startThread();
}

AudioRecorder::~AudioRecorder()
{
    stop();
    backgroundThread.stopThread(2000);
}

juce::String AudioRecorder::start (const juce::File& file, double sampleRate, int numChannels,
                                   int bar)
{
    stop();

    if (file == juce::File())
        return "No file to record into.";

    if (sampleRate <= 0.0)
        return "The audio device is not running.";

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    auto fileStream = std::make_unique<juce::FileOutputStream> (file);

    if (! fileStream->openedOk())
        return "Could not write to " + file.getFullPathName() + ".";

    // The non-deprecated createWriterFor takes ownership through a base-typed
    // unique_ptr, and only on success - a failure leaves the stream with us.
    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    juce::WavAudioFormat format;

    // 24-bit to match what the offline renderer writes by default: a take that
    // came out quieter than intended still has headroom to be lifted.
    const auto writerOptions = juce::AudioFormatWriterOptions()
                                   .withSampleRate (sampleRate)
                                   .withNumChannels (juce::jmax (1, numChannels))
                                   .withBitsPerSample (24);

    auto writer = format.createWriterFor (stream, writerOptions);

    if (writer == nullptr)
        return "Could not create a WAV writer for " + file.getFullPathName() + ".";

    // ThreadedWriter takes the raw pointer and owns it from here.
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
        writer.release(), backgroundThread, 32768);

    destination = file;
    punchInBar = juce::jmax (0, bar);
    samplesRecorded.store (0);

    {
        const juce::ScopedLock lock (writerLock);
        threadedWriter = std::move (threaded);
        activeWriter = threadedWriter.get();
    }

    recording.store (true);
    return {};
}

juce::File AudioRecorder::stop()
{
    if (! recording.load())
        return {};

    // Clear the audio thread's pointer FIRST and under the lock, so the writer
    // cannot be destroyed while a block is still being pushed into it.
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;

    {
        const juce::ScopedLock lock (writerLock);
        activeWriter = nullptr;
        finished = std::move (threadedWriter);
    }

    recording.store (false);

    // Destroying the ThreadedWriter flushes the FIFO and closes the file. It
    // has to happen before the file is read back, which is why it is done here
    // rather than left to the next start().
    finished.reset();

    return samplesRecorded.load() > 0 ? destination : juce::File();
}

void AudioRecorder::writeBlock (const float* const* inputChannelData, int numInputChannels,
                                int numSamples) noexcept
{
    if (inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    auto peak = 0.0f;

    for (int channel = 0; channel < numInputChannels; ++channel)
        if (inputChannelData[channel] != nullptr)
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMinAndMax (
                                         inputChannelData[channel], numSamples).getEnd());

    // Latest-wins maximum, like the mixer meters: a reader that missed a peak
    // between two frames would rather see the louder one.
    auto previous = inputPeak.load (std::memory_order_relaxed);

    while (peak > previous
           && ! inputPeak.compare_exchange_weak (previous, peak, std::memory_order_relaxed))
    {
    }

    // A try-lock, not a lock: the render path must never wait on the message
    // thread swapping a writer in or out.
    const juce::ScopedTryLock lock (writerLock);

    if (! lock.isLocked() || activeWriter == nullptr)
        return;

    if (activeWriter->write (inputChannelData, numSamples))
        samplesRecorded.fetch_add (numSamples, std::memory_order_relaxed);
}

float AudioRecorder::readAndClearInputPeak() noexcept
{
    return inputPeak.exchange (0.0f, std::memory_order_relaxed);
}

} // namespace dew
