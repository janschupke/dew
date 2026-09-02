#pragma once

#include <atomic>
#include <vector>

#include "MixerBus.h"
#include "Sequencer.h"
#include "SnapshotBridge.h"
#include "SynthChannel.h"
#include "Transport.h"

namespace dew
{

/** The renderer.

    Deliberately knows nothing about audio devices: it is prepared with a sample
    rate and a block size, and fills a buffer. Live playback and offline
    rendering therefore run the SAME code, which is what makes it possible to
    prove playback in a test rather than by listening.
*/
class AudioEngine
{
public:
    AudioEngine();

    void prepare (double sampleRate, int maximumBlockSize);
    void releaseResources();

    /** Message thread: hand over a new project state. */
    void setProject (const juce::ValueTree& project, juce::StringArray* warnings = nullptr);

    /** Message thread: hand over a prebuilt snapshot. */
    void publish (EngineSnapshot snapshot);

    // --- transport, callable from any thread ---------------------------------
    void play();
    void stop();
    void rewind();
    void setMode (Transport::Mode);
    Transport::Mode getMode() const noexcept  { return requestedMode.load(); }
    bool isPlaying() const noexcept           { return playing.load(); }

    /** Which pattern plays in pattern mode, by pattern id. */
    void setCurrentPatternId (int patternId) noexcept  { requestedPatternId.store (patternId); }
    int getCurrentPatternId() const noexcept           { return requestedPatternId.load(); }

    /** Playhead in steps, for drawing. Written by the audio thread. */
    double getPlayheadSteps() const noexcept;

    /** Audio thread: renders one block. `buffer` must be stereo. */
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

    double getSampleRate() const noexcept  { return currentSampleRate; }

private:
    void applySnapshotIfChanged (const EngineSnapshot&) noexcept;

    SnapshotBridge bridge;
    Transport transport;

    std::vector<SynthChannel> channels;

    // Preallocated scratch: one mono buffer per channel, one stereo pair per
    // mixer track, all sized in prepare() so processBlock never allocates.
    juce::AudioBuffer<float> channelBuffers;
    juce::AudioBuffer<float> mixerBuffers;

    std::vector<NoteTrigger> triggers;

    juce::uint64 appliedGeneration = 0;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    std::atomic<bool> playing { false };
    std::atomic<bool> rewindRequested { false };
    std::atomic<Transport::Mode> requestedMode { Transport::Mode::pattern };
    std::atomic<int> requestedPatternId { 1 };
    std::atomic<juce::int64> playheadSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace dew
