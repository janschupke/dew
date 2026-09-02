#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "MixerBus.h"
#include "SignalTap.h"
#include "PreviewQueue.h"
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

    /** Moves the transport to a position, in steps from the start.

        Follows the same request-atomic shape as rewind(): the audio thread
        owns the Transport, so the conversion to samples happens there, where
        the sample rate cannot be stale. Voices are reset on arrival for the
        same reason rewind resets them - a note that was sounding before the
        jump has no note-off after it.
    */
    void setPlayheadSteps (double steps);

    void setMode (Transport::Mode);
    Transport::Mode getMode() const noexcept  { return requestedMode.load(); }
    bool isPlaying() const noexcept           { return playing.load(); }

    // --- preview, for auditioning a note outside the sequencer ---------------
    /** Message thread. Sounds a note on a channel until previewNoteOff, so
        clicking a piano key makes the sound the key is for.

        Returns false if the queue was full and the note was dropped.
    */
    bool previewNoteOn (int channelIndex, int pitch, float velocity) noexcept;
    bool previewNoteOff (int channelIndex, int pitch) noexcept;
    bool previewAllOff() noexcept;

    // --- MIDI input, from the MIDI thread ------------------------------------
    /** The same three events, from a hardware controller.

        A SEPARATE queue from the preview one, and that is the whole point.
        PreviewQueue is single-producer by construction - the writer owns
        writeIndex, the reader owns readIndex - and the preview queue's producer
        is the message thread. MIDI callbacks arrive on the OS MIDI thread, so
        sharing one queue would put two producers on one writeIndex, which is a
        data race rather than a policy choice.

        Two queues keep that invariant exactly as written for each, and add no
        new concurrency primitive - the same move the effect pool makes.
    */
    bool midiNoteOn (int channelIndex, int pitch, float velocity) noexcept;
    bool midiNoteOff (int channelIndex, int pitch) noexcept;
    bool midiAllOff() noexcept;

    // --- continuous controllers, callable from any thread --------------------
    /** Pitch bend in semitones, and vibrato depth 0..1, for one channel.

        Plain atomics rather than the queue, and deliberately so. PreviewQueue
        earns its ring because both halves of a click matter: press and release
        can land inside one block and latest-wins would lose the press. A wheel
        position has no halves - only the newest value is audible, dropping the
        ones in between is inaudible, and a sweep sends hundreds of messages a
        second, which is exactly the traffic that would fill a 64-entry ring and
        start refusing NOTES.

        So: notes take the ring, controllers take latest-wins atomics, in the
        same idiom as the meters.
    */
    void setChannelBend (int channelIndex, float semitones) noexcept;
    void setChannelModulation (int channelIndex, float amount) noexcept;

    float getChannelBend (int channelIndex) const noexcept;
    float getChannelModulation (int channelIndex) const noexcept;

    /** Returns every channel's controllers to rest. */
    void resetControllers() noexcept;

    // --- metering ------------------------------------------------------------
    /** Peak level of a mixer track since the last read, 0..1.

        Read-and-clear: the message thread takes the peak and resets it, so a
        meter falls when the music stops instead of holding its highest value
        forever. Plain atomics, like getPlayheadSteps - no new bridge.
    */
    float readAndClearTrackPeak (int trackIndex) noexcept;
    float readAndClearMasterPeak() noexcept;

    /** The finished master output, for drawing.

        Const on purpose: writing is the audio thread's job, and a const-only
        accessor says so at compile time rather than in a comment. Unlike the
        peaks above this is NOT read-and-clear, so any number of displays can
        watch it without stealing from each other.
    */
    const SignalTap& getSignalTap() const noexcept  { return signalTap; }

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

    /** One stereo scratch, reused per channel: channels are processed one at a
        time, so a chain never needs more than one buffer live at once.
    */
    juce::AudioBuffer<float> channelStereo;

    /** The DSP-state pool. Every unit holds every effect type, prepared at its
        maximum size, so switching a slot's type is a reset rather than an
        allocation and the audio thread never builds anything.
    */
    std::vector<std::unique_ptr<EffectUnit>> effectUnits;

    /** What each unit was last used as. A unit that changes type is reset, so a
        reverb tail cannot leak into the delay that replaced it.
    */
    std::vector<int> effectUnitTypes;

    void runChain (const EffectChainSnapshot&, float* left, float* right, int numSamples) noexcept;

    /** One automation's value at the current position, already in the
        parameter's own units, with its target resolved.
    */
    struct ActiveAutomation
    {
        AutomationScope scope = AutomationScope::channel;
        int targetIndex = -1;
        int slotIndex = -1;
        AutomationParam param = AutomationParam::none;
        float value = 0.0f;
    };

    /** Evaluates every automation clip covering this position, once per block.

        Per block rather than per sample: a curve moving over bars does not need
        sample accuracy, and evaluating it once keeps the render path free of
        searching.
    */
    void collectAutomation (const EngineSnapshot&, double positionSteps) noexcept;

    /** Applies whatever automation is pointed at this channel or track.

        Takes a mutable copy of the snapshot's entry rather than editing the
        snapshot: the snapshot is shared, read-only, and the same one may be
        rendered again on the next block.
    */
    void applyAutomation (ChannelSnapshot&, int channelIndex) const noexcept;
    void applyAutomation (MixerTrackSnapshot&, int trackIndex) const noexcept;
    float automatedMasterGain (float base) const noexcept;

    std::vector<ActiveAutomation> activeAutomation;

    std::vector<NoteTrigger> triggers;

    /** One ring per producer thread: the message thread writes previewQueue,
        the MIDI thread writes midiQueue, and the audio thread drains both.
    */
    PreviewQueue previewQueue;
    PreviewQueue midiQueue;

    std::array<std::atomic<float>, kMaxChannels> channelBend {};
    std::array<std::atomic<float>, kMaxChannels> channelModulation {};

    std::array<std::atomic<float>, kMaxMixerTracks> trackPeaks {};
    std::atomic<float> masterPeak { 0.0f };

    SignalTap signalTap;

    static void recordPeak (std::atomic<float>&, const float* left, const float* right,
                            int numSamples, float scale) noexcept;

    void drainPreviewQueue (const EngineSnapshot&) noexcept;
    void applyPreviewEvent (const EngineSnapshot&, const PreviewEvent&, int numChannels) noexcept;

    juce::uint64 appliedGeneration = 0;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    std::atomic<bool> playing { false };
    std::atomic<bool> rewindRequested { false };
    std::atomic<bool> seekRequested { false };
    std::atomic<double> seekToSteps { 0.0 };
    std::atomic<Transport::Mode> requestedMode { Transport::Mode::pattern };
    std::atomic<int> requestedPatternId { 1 };
    std::atomic<juce::int64> playheadSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace dew
