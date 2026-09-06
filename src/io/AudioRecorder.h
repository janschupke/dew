#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

namespace dew
{

/** Captures the audio input to a WAV file.

    The writing is juce::AudioFormatWriter::ThreadedWriter, which is JUCE's own
    answer to exactly this problem: a lock-free FIFO the audio thread pushes
    into and a background thread that drains it to disk. Hand-rolling the same
    thing would add a fourth concurrency primitive to a codebase that has been
    careful to have three, and it would be the one carrying the take.

    The audio thread's only contract is writeBlock(). It takes a try-lock and
    gives up rather than waiting, so a stop() that lands mid-block costs one
    block of the recording instead of a priority inversion in the render path.
*/
class AudioRecorder
{
public:
    AudioRecorder();
    ~AudioRecorder();

    /** Message thread. Begins writing to `file`, which is overwritten.

        `punchInBar` is remembered, not used: it is what the clip needs when the
        take is over, and the transport will have moved on by then.

        `preRollSamples` is a count-in: that many input samples are METERED and
        then discarded before anything is written, so the file begins at the
        downbeat rather than a bar of clicks earlier. It is counted down here
        rather than by skipping the call, because writeBlock's contract is that
        the meter runs whether or not a take does - and the level display is
        exactly what somebody is watching during a count-in.

        The engine's own count-in budget counts down by the same numSamples in
        the same device callback, so the two cannot disagree about which block
        the take starts on. See AudioEngine::playWithCountIn.
    */
    juce::String start (const juce::File& file, double sampleRate, int numChannels, int punchInBar,
                        juce::int64 preRollSamples = 0);

    /** Whether the take is still discarding its count-in. */
    bool isPreRolling() const noexcept
    {
        return preRoll.load() > 0;
    }

    /** Message thread. Finishes the file and returns it, or an invalid File if
        nothing was recorded.
    */
    juce::File stop();

    bool isRecording() const noexcept
    {
        return recording.load();
    }

    /** The bar the take started on. Meaningful once stop() has returned. */
    int getPunchInBar() const noexcept
    {
        return punchInBar;
    }

    juce::File getFile() const
    {
        return destination;
    }

    /** How many frames have been captured. Message thread, for a duration
        readout while a take is running.
    */
    juce::int64 getNumSamplesRecorded() const noexcept
    {
        return samplesRecorded.load();
    }

    /** Audio thread. Pushes one block of input, and updates the meter.

        Safe to call when nothing is recording - the meter still runs, which is
        what lets the settings panel show input level before a take starts.
    */
    void writeBlock (const float* const* inputChannelData, int numInputChannels,
                     int numSamples) noexcept;

    /** Peak input level since the last read, 0..1.

        Read-and-clear, the same shape as AudioEngine's track meters, so a
        meter falls when the signal stops instead of holding its highest value.
    */
    float readAndClearInputPeak() noexcept;

private:
    juce::TimeSliceThread backgroundThread { "dew recorder" };

    /** Written on the message thread, read on the audio thread under the lock.
        The lock is only ever contended for the length of a pointer swap.
    */
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;

    /** What the audio thread uses, so it never touches the unique_ptr itself. */
    juce::AudioFormatWriter::ThreadedWriter* activeWriter = nullptr;

    juce::File destination;
    int punchInBar = 0;

    std::atomic<bool> recording { false };
    std::atomic<juce::int64> preRoll { 0 };
    std::atomic<juce::int64> samplesRecorded { 0 };
    std::atomic<float> inputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};

} // namespace dew
