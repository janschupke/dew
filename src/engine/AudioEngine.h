#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "engine/MixerBus.h"
#include "engine/SignalTap.h"
#include "engine/EffectModulePool.h"
#include "engine/PreviewQueue.h"
#include "engine/Sequencer.h"
#include "engine/SnapshotBridge.h"
#include "engine/InstrumentModule.h"
#include "engine/Transport.h"
#include "model/Constants.h"

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

    /** Where setProject gets audio for audio channels.

        A pointer the owner sets rather than a member, because the pool outlives
        any one project and because the offline renderer and every engine test
        build snapshots with no audio at all. Null leaves audio channels silent.
    */
    void setSamplePool (SampleProvider* provider) noexcept
    {
        samplePool = provider;
    }

    SampleProvider* getSamplePool() const noexcept
    {
        return samplePool;
    }

    /** How many effect modules exist. For the test that pins the laziness: the
        pool used to build every type in every unit up front, whether or not a
        project used one. */
    int getMaterialisedEffectModuleCount() const noexcept
    {
        return modulePool.materialisedCount();
    }

    /** How many instrument modules exist, by kind. For the test that pins the
        laziness: sixty-four SynthChannels - a thousand and twenty-four voices -
        used to exist whether a project had a synth in it or not. */
    int getMaterialisedInstrumentCount (InstrumentType) const noexcept;

    /** The reserved size of the two vectors the render path fills each block.

        For the allocation gate: a vector that grew on the audio thread has a
        different capacity afterwards, which is what turns "something allocated"
        into "this allocated". Both are reserved in the constructor and again in
        prepare(), and nothing on the render path may push past the bound. */
    std::size_t getTriggerCapacity() const noexcept
    {
        return triggers.capacity();
    }
    std::size_t getActiveAutomationCapacity() const noexcept
    {
        return activeAutomation.capacity();
    }

    /** How many automation entries the last block actually collected.

        The gate's fixture has to prove it loaded this vector past its reserve,
        or "nothing allocated" only means "nothing was asked to". */
    std::size_t getActiveAutomationCount() const noexcept
    {
        return activeAutomation.size();
    }

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

    // --- the loop window -----------------------------------------------------
    /** A user's loop, in steps, half-open [start, end). One indivisible pair.

        Latest-wins atomics rather than a request flag, for the reason the
        controllers below give: only the newest range is audible, and a drag
        rewrites it hundreds of times a second - exactly the traffic that would
        fill a ring and start refusing notes.

        ONE atomic per region rather than two, and that is the load-bearing part.
        Two would let the audio thread read a new start beside an old end - a
        range of negative width, so a block with NO loop at all, heard as the
        playhead escaping the region for one buffer and returning at the wrong
        phase. Eight bytes is lock-free everywhere this runs, so making the tear
        unrepresentable costs nothing.

        One region PER MODE, because the piano roll selects steps of a pattern
        and the playlist selects bars of the song, and which of them is audible
        is whichever mode the transport is in. Keyed by mode rather than
        re-pushed on a mode change: the mode is set from two places that do not
        go through the editor state, and a single region would be wrong for a
        block whenever one of them forgot.

        The range is CLAMPED to the material on the audio thread, not here: how
        long the material is comes from the snapshot, and only the audio thread
        holds a stable one. A range that is empty after clamping - a loop wholly
        past the end of a short pattern - is not a loop, and the material's own
        extent wraps as it always did.

        The ends are ordered for you, so a right-to-left drag means what it looks
        like.

        Live playback only. An offline render builds its own AudioEngine and
        takes its scope from RenderOptions::barRange - see OfflineRenderer.
    */
    struct LoopRegion
    {
        float startSteps = 0.0f;
        float endSteps = 0.0f;

        bool isEmpty() const noexcept
        {
            return endSteps <= startSteps;
        }

        bool operator== (const LoopRegion& other) const noexcept
        {
            // exactlyEqual, not ==: these are stored, compared and never
            // arithmetic'd, so bit equality is what "the same region" means -
            // and the CI preset builds with -Werror on -Wfloat-equal.
            return juce::exactlyEqual (startSteps, other.startSteps)
                   && juce::exactlyEqual (endSteps, other.endSteps);
        }
    };

    void setLoopRangeSteps (Transport::Mode, double startSteps, double endSteps) noexcept;
    void clearLoopRange (Transport::Mode) noexcept;

    /** What the UI draws: what was ASKED for, unclamped.

        Unclamped on purpose - a loop bracket must not crawl when a pattern is
        shortened underneath it, and every ruler already dims what is past the
        material, so a loop hanging off the end reads as inert without a second
        copy of the same numbers written from the audio thread.
    */
    LoopRegion getLoopRegion (Transport::Mode) const noexcept;
    bool hasLoopRegion (Transport::Mode) const noexcept;

    void setMode (Transport::Mode);
    Transport::Mode getMode() const noexcept
    {
        return requestedMode.load();
    }
    bool isPlaying() const noexcept
    {
        return playing.load();
    }

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
    const SignalTap& getSignalTap() const noexcept
    {
        return signalTap;
    }

    /** Which pattern plays in pattern mode, by pattern id. */
    void setCurrentPatternId (int patternId) noexcept
    {
        requestedPatternId.store (patternId);
    }
    int getCurrentPatternId() const noexcept
    {
        return requestedPatternId.load();
    }

    /** Playhead in steps, for drawing. Written by the audio thread. */
    double getPlayheadSteps() const noexcept;

    /** Audio thread: renders one block. `buffer` must be stereo. */
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

    double getSampleRate() const noexcept
    {
        return currentSampleRate;
    }

private:
    void applySnapshotIfChanged (const EngineSnapshot&) noexcept;

    SnapshotBridge bridge;
    Transport transport;

    SampleProvider* samplePool = nullptr;

    /** Each channel's instruments, made on the message thread and never
        destroyed - the same lifetime rule the effect pool follows, and for the
        same reason: a published snapshot may still be being rendered.

        Both kinds are held rather than one, because a channel can be switched
        between them and its voices should survive the trip back.
    */
    struct ChannelInstruments
    {
        /** Indexed by InstrumentType rather than one member per kind, so a
            third kind is a wider array and one row in ModuleFactory's switch
            rather than a third member and the five places that named the other
            two. Nothing here is ever destroyed - see above. */
        std::array<std::unique_ptr<InstrumentModule>, (size_t) kNumInstrumentTypes> byType;
    };

    std::vector<ChannelInstruments> instruments;

    /** This block's note events, per channel.

        Bucketed on the audio thread from the preview queues and the sequencer's
        triggers, then handed to each instrument as one span - which is the
        shape a plugin receives notes in, and which lets a module see a whole
        block's timing rather than being poked once per note.

        Reserved in the constructor and NEVER grown past it: push_back on a
        vector at capacity allocates, and this is filled on the audio thread.
        Past the bound an event is refused, which is the contract PreviewQueue
        already has when its ring is full - a bounded queue that drops is
        honest; one that allocates to avoid dropping is not realtime.
    */
    std::vector<std::vector<NoteEvent>> channelEvents;

    /** The most events one channel can receive in one block. Both preview rings
        can empty into a single channel, and the sequencer's trigger list is
        reserved at 256. */
    static constexpr int maxEventsPerChannel = 2 * PreviewQueue::capacity + 256;

    /** Appends unless the channel is already at its bound. */
    void pushNoteEvent (int channelIndex, const NoteEvent&) noexcept;

    InstrumentModule* instrumentFor (int channelIndex, InstrumentType) noexcept;
    void resetAllInstruments() noexcept;

    // Preallocated scratch: one mono buffer per channel, one stereo pair per
    // mixer track, all sized in prepare() so processBlock never allocates.
    juce::AudioBuffer<float> channelBuffers;
    juce::AudioBuffer<float> mixerBuffers;

    /** One stereo scratch, reused per channel: channels are processed one at a
        time, so a chain never needs more than one buffer live at once.
    */
    juce::AudioBuffer<float> channelStereo;

    /** The DSP-state pool. Modules are made on the message thread on first use
        and never destroyed while the engine lives - see EffectModulePool for
        why the second half of that is a correctness rule rather than thrift.
    */
    EffectModulePool modulePool;

    /** One dry copy for the wet/dry mix.

        One, not one per unit: chains run strictly one at a time, so a buffer
        per pool unit was thirty-two copies of a scratch that only ever had a
        single live user.
    */
    juce::AudioBuffer<float> dryScratch;

    /** What each unit was last used as. A unit that changes type is reset, so a
        reverb tail cannot leak into the delay that replaced it.
    */
    std::vector<int> effectUnitTypes;

    void runChain (const EffectChainSnapshot&, float* left, float* right, int numSamples) noexcept;

    /** Points every slot at its DSP. Message thread, between building a
        snapshot and publishing it. */
    void resolveModules (EngineSnapshot&);

    /** One automation's value at the current position, already in the
        parameter's own units, with its target resolved.
    */
    struct ActiveAutomation
    {
        AutomationScope scope = AutomationScope::channel;
        int targetIndex = -1;
        int slotIndex = -1;
        AutomationParam param = AutomationParam::none;
        int paramIndex = -1; ///< into the effect slot's block; -1 for other scopes
        float value = 0.0f;
    };

    /** Evaluates every automation clip covering this position, once per block.

        Per block rather than per sample: a curve moving over bars does not need
        sample accuracy, and evaluating it once keeps the render path free of
        searching.
    */
    void collectAutomation (const EngineSnapshot&, double positionSteps) noexcept;

    /** The only things automation may move on a channel, and nothing else.

        A distinct type rather than a copy of ChannelSnapshot, and that is the
        entire point. ChannelSnapshot carries a shared_ptr to the channel's
        audio, and EngineSnapshot.h spends fourteen lines explaining that every
        copy and release of that pointer must happen on the message thread -
        calling the alternative "a correctness bug, not a style question".

        The render loop copied one per channel per block anyway: up to
        sixty-four atomic refcount pairs on the audio thread, every block. The
        pointer could not actually reach zero, because the snapshot slot held a
        reference of its own, so nothing ever crashed and nothing ever said so.

        This struct has no such member. The type system states the invariant
        now, instead of a comment asking for it.
    */
    /** The map the MESSAGE thread converts with, replaced in publish().

        A plain member rather than an atomic, because the audio thread reads the
        one inside the snapshot it has latched and never this. setPlayheadSteps
        and getPlayheadSteps are its only readers and both run on the message
        thread - the doc on setPlayheadSteps said "any thread" and no caller ever
        did.
    */
    std::shared_ptr<const TempoMap> uiTempoMap;

    struct ChannelOverrides
    {
        float volume = 0.0f;
        float pan = 0.0f;

        /** Mute, because a curve over it is a curve over a VALUE on this
            channel. Solo is deliberately not here: it is a relation between
            channels, and anyChannelSolo is a snapshot-wide precomputation the
            audio thread cannot redo every block. */
        bool muted = false;

        OscBankSnapshot osc;
        EffectChainSnapshot effects;
    };

    struct MixerTrackOverrides
    {
        float gain = 0.0f;
        float pan = 0.0f;
        bool mute = false;
        EffectChainSnapshot effects;
    };

    /** Whatever automation points at this channel, or null if none does.

        Null rather than a filled-in copy, so a block that automates nothing
        copies nothing - which is every block of most projects. Sized in
        prepare(), so the render path allocates nothing either.
    */
    const ChannelOverrides* overridesFor (const ChannelSnapshot&, int channelIndex) noexcept;
    const MixerTrackOverrides* overridesFor (const MixerTrackSnapshot&, int trackIndex) noexcept;
    float automatedMasterGain (float base) const noexcept;

    /** The invariant, enforced rather than requested.

        ChannelSnapshot is NOT trivially copyable - it carries the shared_ptr to
        a channel's audio - and that is exactly why the render path may not copy
        one. Overrides are, and always must be: the day someone adds a member
        here that owns something, this fails to compile instead of putting a
        refcount back on the audio thread.
    */
    static_assert (std::is_trivially_copyable_v<ChannelOverrides>,
                   "a channel override must own nothing: the audio thread copies it every block");
    static_assert (std::is_trivially_copyable_v<MixerTrackOverrides>,
                   "a mixer track override must own nothing, for the same reason");

    std::vector<ChannelOverrides> channelOverrides;
    std::vector<MixerTrackOverrides> trackOverrides;

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

    double currentSampleRate = kDefaultSampleRate;
    int currentBlockSize = kDefaultBlockSize;

    std::atomic<bool> playing { false };
    std::atomic<bool> rewindRequested { false };
    std::atomic<bool> seekRequested { false };
    std::atomic<double> seekToSteps { 0.0 };
    std::atomic<Transport::Mode> requestedMode { Transport::Mode::pattern };

    /** One per Transport::Mode, indexed by loopSlotFor(). */
    std::array<std::atomic<LoopRegion>, 2> loopRegions {};

    /** The range the audio thread last acted on, so a CHANGE can be told from
        the same range arriving again every block. Audio-thread only, and so not
        an atomic.
    */
    LoopRegion lastAppliedLoop;

    /** The wrap window processBlock last APPLIED - a user's loop clamped to the
        material, or the material's own extent when there is none.

        Published so setPlayheadSteps can predict the fold the next block will
        do. It cannot derive the window itself: the material's length comes from
        the snapshot, and only the audio thread holds a stable one. Reading the
        window the audio thread last used is at worst one block stale, which is
        the same block that is about to apply it.
    */
    std::atomic<LoopRegion> appliedWrap {};
    std::atomic<int> requestedPatternId { 1 };
    std::atomic<juce::int64> playheadSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace dew
