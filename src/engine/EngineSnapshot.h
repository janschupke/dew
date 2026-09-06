#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <memory>
#include <vector>

#include "engine/TempoMap.h"
#include "model/AutomationCurve.h"
#include "model/AutomationTargets.h"
#include "engine/SoundFontProvider.h"
#include "engine/SoundFontVoice.h"
#include "model/InstrumentType.h"
#include "model/ProjectSchema.h"
#include "engine/Effects.h"
#include "engine/Module.h"
#include "model/Constants.h"

namespace dew
{

/** Limits on what the realtime engine will render.

    The audio thread must not allocate, so its voice pools and mix buffers are
    sized once in prepare(). That requires an upper bound. These are far above
    anything a prototype project reaches; buildSnapshot() clamps to them and
    reports what it dropped rather than silently truncating.
*/
inline constexpr int kMaxChannels = 64;
inline constexpr int kMaxVoicesPerChannel = 16;

// kMaxMixerTracks is in model/ProjectSchema.h, included above: once inserts
// could be added and removed it became a rule about what a DOCUMENT may hold,
// and the engine is one of its readers rather than its owner.

/** How many effect slots in the whole project can hold live DSP state.

    Each unit preallocates every effect type at once - a delay line, reverb
    tanks, a chorus - so it is a few hundred kilobytes. Capping the pool is what
    makes preallocation possible at all, and preallocation is what lets effect
    topology travel in the snapshot instead of needing a command queue and a
    pointer swap on the audio thread.
*/
/** One DSP unit per effect the document can hold, so a chain is never dropped.

    It was 32, while the schema permits four effects on each of 64 channels and
    32 mixer tracks plus the master - 388. Past 32, buildSnapshot warned and
    then stopped reading that chain, and the warning went to an argument whose
    default was nullptr. Raising it costs nothing now that modules are made on
    first use rather than all at once.
*/
inline constexpr int kMaxEffectUnits = kMaxChannels * kMaxEffectsPerChain
                                       + kMaxMixerTracks * kMaxEffectsPerChain
                                       + kMaxEffectsPerChain;
inline constexpr int kMaxAutomations = 32;

/** How many note-ons one block may start, across every channel.

    A bound rather than a hope. `Sequencer::collect` appends one trigger per note
    starting on a step boundary inside the block, and a project can put a note on
    the same step of all 64 channels; the vector it fills is reserved once and
    reused, so a project past this bound would have grown it on the audio thread.

    Same contract as maxEventsPerChannel: refuse at the bound. A block that drops
    a note is honest; one that allocates to keep it is not realtime.
*/
inline constexpr int kMaxTriggersPerBlock = 512;

/** One oscillator slot, resolved.

    Carries both modes' settings, the way EffectSnapshot carries every effect
    type's. `wave` is read in classic mode and the wavetable fields in wavetable
    mode; the unused half costs a few bytes and saves the audio thread a branch
    on which shape of struct it was handed.

    `table` is an INDEX into the factory bank, never a pointer, so the whole
    snapshot stays trivially copyable.
*/
struct OscSettings
{
    bool enabled = true;
    OscMode mode = OscMode::classic;

    Waveform wave = Waveform::saw;

    int table = 0;
    float position = 0.0f;    ///< 0..1 through the table's frames
    float positionMod = 0.0f; ///< -1..1, added to position over the note
    PositionSource positionSource = PositionSource::envelope;
    float positionRate = 1.0f; ///< Hz, when the source is the LFO
    int unisonVoices = 1;
    float unisonDetune = 0.0f; ///< cents, edge to edge

    int octave = 0;
    float detuneCents = 0.0f;
    float gain = 0.8f;

    // --- the slot's LFO ------------------------------------------------------
    //
    // One per slot, moving any combination of pitch, level and position in the
    // field. Everything here is a plain value: this struct travels in the
    // snapshot and ChannelOverrides asserts it is trivially copyable.
    bool lfoOn = false;
    Waveform lfoWave = Waveform::sine;

    /** Whether the rate is locked to the tempo. Kept even though `lfoHz` is
        already resolved, because automation needs it: a curve over the rate of
        a SYNCED LFO has to be ignored rather than quietly unsync it. */
    bool lfoSync = false;

    /** The rate in Hz, ALREADY RESOLVED - the free rate, or what the division
        works out to at the project tempo. Resolved by the reader on the message
        thread, the way SampleSettings::pitchRatio is.

        The render path must not derive this from the transport instead:
        Transport::samplesPerStep() is the rate at the top of the current block
        whenever the tempo map is not constant, so an increment computed from it
        every block would make a synced LFO's phase depend on the buffer size -
        which is exactly what the block-size-invariance render test pins. */
    float lfoHz = 1.0f;

    float lfoToPitch = 0.0f;  ///< semitones, either way
    float lfoToVolume = 0.0f; ///< -1..1
    float lfoToPan = 0.0f;    ///< -1..1

    // --- the slot's row of the FM matrix -------------------------------------
    //
    // How far this oscillator bends each slot's phase, and how much of it is
    // heard. Plain values in a fixed array, so the struct stays trivially
    // copyable the way the LFO's do.
    //
    // `fmTo[i]` is indexed by the DESTINATION slot, including this slot itself -
    // the diagonal is feedback, and the voice reads a modulator one sample late
    // whichever cell it came from, so it needs no special case.
    std::array<float, kMaxOscillators> fmTo {};

    /** How much of this oscillator reaches the output, multiplying `gain`.

        One rather than zero, and that is the whole compatibility story: three
        oscillators at full output with every amount at zero is three
        oscillators summed in parallel, which is what this synth was.
    */
    float fmOut = 1.0f;

    /** On AND actually asking for something. Precomputed, and load-bearing: a
        voice puts only its active slots in the LFO pass, so a slot switched on
        with all three depths at zero stays in the plain pass and renders the
        same floats in the same order it did before there were LFOs at all. */
    bool lfoActive = false;
};

/** A channel's oscillators, resolved.

    Fixed size for the same reason EffectChainSnapshot is: a snapshot is copied
    onto the audio thread, and the audio thread never chases a pointer it did
    not allocate. Disabled slots travel with the rest rather than being dropped,
    so a voice can be started from the bank alone.
*/
struct OscBankSnapshot
{
    std::array<OscSettings, kMaxOscillators> slots;
    int numSlots = 0;
    bool anyEnabled = false; ///< precomputed, so nothing downstream has to scan

    /** Whether the FM matrix is asking for anything at all: an enabled slot
        routing into an enabled slot, or one whose output is not exactly full.

        Precomputed and load-bearing, exactly as OscSettings::lfoActive is. A
        voice takes its old render path whenever this is false, so every project
        that predates the matrix - which is every project, since the defaults
        ARE the old behaviour - renders the same floats in the same order.
    */
    bool anyFm = false;
};

struct AmpSettings
{
    float attack = 0.005f, decay = 0.12f, sustain = 0.7f, release = 0.15f;
};

/** One effect slot, resolved.

    `unitIndex` is the pool entry that holds this slot's DSP state. It is
    derived from the effect's persistent id, not from its position, so adding an
    effect to one channel does not renumber every effect after it and cut the
    tails they were in the middle of.
*/
struct EffectSnapshot
{
    int id = 0; ///< the document's effect id, which keys the DSP pool
    EffectType type = EffectType::filter;
    bool enabled = true;
    int unitIndex = -1;
    EffectParamBlock params {};

    /** The DSP for this slot, resolved on the message thread.

        Raw and non-owning, and safe for one reason: EffectModulePool never
        destroys a module while it lives, so this cannot dangle. The same idiom
        SynthVoice uses for its `const Wavetable*`, and for the same reason -
        SnapshotBridge cannot tell the message thread when the audio thread has
        finished with an old snapshot, so anything a snapshot points at must
        outlive every snapshot.
    */
    EffectModule* module = nullptr;
};

/** A chain of effect slots. Fixed size so a snapshot is trivially copyable and
    the audio thread never chases a pointer it did not allocate.
*/
struct EffectChainSnapshot
{
    std::array<EffectSnapshot, kMaxEffectsPerChain> slots;
    int numSlots = 0;

    /** True if any slot would actually do something.

        Written out twice before this - once in buildSnapshot to decide
        `anyEffects`, once in the offline renderer to decide whether a stem needs
        the stereo path - which is two chances for "an effect chain that matters"
        to come to mean two different things.
    */
    bool anyEnabled() const noexcept
    {
        for (int i = 0; i < numSlots; ++i)
            if (slots[(size_t) i].enabled)
                return true;

        return false;
    }
};

/** An audio channel's playback settings, resolved.

    Every field is in the SOURCE file's own frames and its own sample rate, not
    the engine's. Trim, fades and length are all properties of the recording,
    and expressing them in output frames would silently move every one of them
    the first time the project opened on a device running at another rate.

    `pitchRatio` is the semitone transpose already turned into a frequency
    ratio: the message thread does the std::pow, the render path does not.
*/
struct SampleSettings
{
    int startSample = 0;
    int endSample = 0; ///< exclusive; the buffer length when the document says 0
    int fadeInSamples = 0;
    int fadeOutSamples = 0;
    float pitchRatio = 1.0f;
    double sourceSampleRate = kDefaultSampleRate;
    bool reverse = false;
    bool loop = false;
};

struct ChannelSnapshot
{
    int id = 0;
    int mixerTrackIndex = 0; ///< resolved to an index, so the audio thread never searches
    float volume = 0.8f;
    float pan = 0.0f;
    bool muted = false;
    OscBankSnapshot osc;
    AmpSettings amp;
    EffectChainSnapshot effects;

    InstrumentType source = InstrumentType::synth;
    SampleSettings sample;
    SoundFontSettings soundFontSettings;

    /** The audio an "audio" channel plays, or null.

        The ONE member of any snapshot that is not trivially copyable, and it is
        safe for a specific reason worth writing down. SnapshotBridge::acquire()
        hands the audio thread a REFERENCE to a slot; the audio thread never
        copies a snapshot and never destroys one. Every copy and every release
        of this pointer therefore happens on the message thread, inside
        publish(), where an allocation is allowed.

        A future change that copies a snapshot on the audio thread would turn a
        refcount decrement into a possible deallocation in the render path. That
        would be a correctness bug, not a style question.
    */
    std::shared_ptr<const juce::AudioBuffer<float>> audio;

    /** The soundfont a "soundfont" channel plays, or null.

        The SECOND member that is not trivially copyable, and it is safe for
        exactly the reason `audio` above is - every copy and every release
        happens on the message thread, inside publish(). The warning there
        applies to both: a change that copied a snapshot on the audio thread
        would turn a refcount decrement into a possible deallocation in the
        render path.
    */
    std::shared_ptr<const SoundFontData> soundFont;
};

struct NoteSnapshot
{
    int channelIndex = -1; ///< resolved; -1 means the note referenced a missing channel
    int step = 0;
    int lengthSteps = 1;
    int pitch = 60;
    float velocity = 1.0f;
};

struct PatternSnapshot
{
    int id = 0;
    int lengthSteps = 16;
    std::vector<NoteSnapshot> notes;
};

/** Which parameter an automation drives, as a code the audio thread can switch
    on. Resolving the property name to this on the message thread is what keeps
    string comparison out of the render path.
*/
enum class AutomationParam
{
    none,
    volume,
    pan,
    gain,
    position,
    cutoff,
    resonance,
    mix,
    roomSize,
    damping,
    width,
    delayMs,
    feedback,
    drive,
    outputGain,
    rate,
    depth,
    lowGainDb,
    midGainDb,
    midFreq,
    highGainDb,
    tone,
    centreFreq,
    threshold,
    ratio,
    attackMs,
    releaseMs,
    makeup,
    ceiling,

    /** Discrete ones. filterMode needs an enumerator even though it travels
        through paramIndex like any other block parameter, because buildSnapshot
        DROPS an automation whose param is `none` - so without a row here a
        filter-mode curve would be silently ignored. */
    filterMode,
    distortionMode,

    /** The rest of an oscillator slot's own. `position` above was the only one
        of the five the engine ever applied, so wavePositionMod,
        wavePositionRate and unisonDetune were declared automatable in the
        catalog, offered by the picker, drawn as curves - and read by nothing. */
    positionMod,
    positionRate,
    unisonDetune,

    /** An oscillator slot's own pitch. Latched at note-on like everything but
        the wavetable position, so a curve here moves the NEXT note. */
    oscOctave,
    oscDetuneCents,

    /** The slot's LFO: how fast it runs and how far it reaches. Unlike the two
        above, these reach a note ALREADY SOUNDING - see SynthVoice::setLfo. An
        LFO whose rate and depth could only change between notes is one nobody
        can dial in, because every adjustment is inaudible until the next note.

        Switching an LFO ON is still a next-note change: the slot is in the
        plain half of the note-on partition and cannot join the other half
        without re-ordering a float sum the pinned renders depend on.

        A curve over `lfoRate` is applied only while the slot is NOT synced -
        see AudioEngineAutomation. Ignoring it is the honest answer; the
        alternative is a curve that silently unsyncs an LFO the user locked to
        the tempo. */
    lfoRate,
    lfoToPitch,
    lfoToVolume,
    lfoToPan,

    /** The slot's row of the FM matrix. Like the wavetable position and the
        LFO, and unlike everything else on a slot, these reach a note ALREADY
        SOUNDING - see SynthVoice::setFmMatrix. An FM index that could only
        change between notes is an FM index that never moves. */
    fmTo1,
    fmTo2,
    fmTo3,
    fmOut,

    /** The amplitude envelope. Spelled apart from the compressor's attackMs and
        releaseMs because they are different parameters in different units on
        different nodes, and one enumerator for two would be exactly the
        drift automationParamFromIdentifier's own comment warns about. */
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,

    /** A soundfont channel's six offsets into the font it plays. */
    sfTranspose,
    sfTuneCents,
    sfFilterOffset,
    sfAttackScale,
    sfReleaseScale,
    sfVelocitySens,

    enabled,    ///< an effect slot's bypass
    oscEnabled, ///< an oscillator slot's on/off
    muted,      ///< a channel's or a mixer track's mute

    /** The arrangement's own tempo. Not applied through an override like the
        rest: it changes how STEPS become time, which is the TempoMap's job. */
    tempoBpm,

    /** Not a parameter: the end of the list, and it must stay last.

        Here so the list can be WALKED. AutomationOverrideTests drives every
        enumerator between `none` and this one through applyAutomation and
        requires that something moved, which is the only way to catch the
        failure this list invites - an enumerator the reader can produce and
        the override ladder has no line for draws a curve on screen and moves
        nothing. Adding one above this is enough to be covered.
    */
    count
};

/** One automation definition, with its target resolved to indices. */
struct AutomationSnapshot
{
    AutomationScope scope = AutomationScope::channel;
    int targetIndex = -1; ///< channel index or mixer track index; -1 for master
    int slotIndex = -1;   ///< effect slot in the chain, -1 when not an effect
    AutomationParam param = AutomationParam::none;

    /** Where this parameter sits in the slot's block, for effect scopes.

        Resolved on the message thread, where the slot's type is known. It is
        what turned applyToEffect from a sixteen-case switch - one case per
        parameter of every type, which had to be extended for each new one -
        into a single indexed write.
    */
    int paramIndex = -1;

    /** What the parameter IS, resolved on the message thread.

        A pointer into a table with static storage duration, so it is trivially
        copyable and safe to read on the audio thread. This used to be the
        range and the curve copied out into three fields here, which was a
        fourth declaration of numbers that had already drifted once.
    */
    const ParamSpec* spec = nullptr;
    /** The curve itself, in the model's own point type.

        Not an engine copy of it. The two evaluators were a hand-copied pair and
        this struct's own point type was the reason they could not simply share
        one: a `float` value here and a `double` there is a second declaration of
        the same number. CurvePoint is trivially copyable and model-layer, so
        holding it costs the snapshot nothing.
    */
    std::vector<CurvePoint> points;

    /** Value at a step, in the parameter's own units. */
    float valueAt (double step) const noexcept;
};

struct ClipSnapshot
{
    int patternIndex = -1;    ///< resolved
    int automationIndex = -1; ///< resolved; >= 0 makes this an automation clip
    int channelIndex = -1;    ///< resolved; >= 0 makes this an audio clip
    /** STEPS, like everything else the sequencer windows against.

        Bars, until clips could start off a bar line. Every reader multiplied by
        stepsPerBar the moment it had one - six of them, in five files - so the
        conversion was already happening everywhere and only the document was
        keeping it in the other unit.
    */
    int startStep = 0;
    int lengthSteps = 16;

    /** Resolved audibility of the playlist track this clip sits on. Folded in
        here so the sequencer never has to look a track up.
    */
    bool trackAudible = true;

    /** How loud that track is, folded in for the same reason.

        A SCALE ON THE TRIGGER, not a bus gain, and that is a limitation worth
        stating rather than hiding. Pan and volume are applied once per channel
        (AudioEngineBlock) and once per mixer track; by the time a signal
        reaches either, the notes every lane contributed have been summed into
        one buffer, so a lane cannot attenuate what it no longer owns. What a
        lane CAN do is decide how hard it hits the note in the first place,
        which is per-trigger and needs no second mixing stage.

        That is also why there is no lane PAN. Position is not expressible as a
        property of a trigger the way loudness is, and a control that silently
        did nothing would be worse than an absent one.
    */
    float trackGain = 1.0f;
};

struct MixerTrackSnapshot
{
    int id = 0;
    float gain = 0.8f;
    float pan = 0.0f;
    bool mute = false;
    EffectChainSnapshot effects;
};

/** Everything the audio thread needs to render, with every cross-reference
    already resolved to an array index. Built on the message thread from the
    ValueTree; read-only once published.
*/
/** The map a snapshot has before anything gives it one: the default tempo, held
    constant.

    A default MEMBER rather than something buildSnapshot remembers to set,
    because "never null" is an invariant everything that converts a step into
    time relies on - and a snapshot assembled by hand in a test is exactly where
    a convention gets forgotten. One allocation for the whole program: it hands
    out a pointer to a function-local static.
*/
const std::shared_ptr<const TempoMap>& defaultTempoMap();

struct EngineSnapshot
{
    double tempoBpm = 128.0;

    /** How steps become time. Never null.

        A shared_ptr for the same reason ChannelSnapshot::audio is one, and with
        the same rule: only the MESSAGE thread ever copies or releases it. The
        audio thread reads through a raw pointer it took from a snapshot it has
        already latched.

        In the snapshot rather than threaded through RenderOptions because
        OfflineRenderer, RenderPanel and MidiExporter each build their own
        snapshot - so anything reachable from one needs no plumbing at all.
    */
    std::shared_ptr<const TempoMap> tempoMap = defaultTempoMap();
    int stepsPerBeat = 4;

    /** The project's meter. `beatsPerBar` groups beats into bars and is what
        every bar computation below divides by; `beatUnit` is notational and the
        engine never reads it - only MidiExporter does, for the time signature.
    */
    int beatsPerBar = 4;
    int beatUnit = 4;

    std::vector<ChannelSnapshot> channels;
    std::vector<PatternSnapshot> patterns;
    std::vector<ClipSnapshot> clips; ///< flattened across all playlist tracks
    std::vector<MixerTrackSnapshot> mixerTracks;
    std::vector<AutomationSnapshot> automations;

    float masterGain = 0.9f;
    EffectChainSnapshot masterEffects;

    // There were three flags here - anySolo, anyChannelSolo,
    // anyPlaylistTrackSolo - because solo was a property of a whole SCOPE
    // rather than of one track: one track soloed silenced every track that was
    // not, so audibility could not be read off a track without first scanning
    // its neighbours. One state per track ends that, and with it the pre-scan,
    // the precomputation and the reason mute could carry a curve while solo
    // could not.

    /** Incremented on every build. The stress test uses it to tell snapshots
        apart; the engine uses it to notice that the document changed.
    */
    juce::uint64 generation = 0;

    int stepsPerBar() const
    {
        return stepsPerBeat * beatsPerBar;
    }

    int patternIndexForId (int patternId) const;
    int channelIndexForId (int channelId) const;

    /** Length of the arrangement in steps: the end of the last clip. Zero when
        the playlist is empty.
    */
    int songLengthSteps() const;

    /** True when the snapshot has nothing that could make a sound. */
    bool isSilent() const;

    /** Whether a channel should sound.

        A method rather than a field read, because the mute it answers from can
        come from an automation curve as well as from the document - see the
        overload below. It used to compose mute with the mixer-wide solo state,
        which is why it took the snapshot as well as the channel.
    */
    bool isChannelAudible (const ChannelSnapshot&) const noexcept;

    /** The same, with mute taken from an automation override rather than from
        the snapshot.

        An overload rather than a mutable field, because the snapshot is shared
        and read by several passes: a stem render and a live block look at the
        same one, and one of them writing a per-block mute into it would be a
        data race with the other.
    */
    bool isChannelAudible (const ChannelSnapshot&, bool mutedOverride) const noexcept;

    /** True if any chain anywhere has an enabled slot - lets the engine skip the
        whole stereo effect stage on a project that uses none.
    */
    bool anyEffects = false;

    /** True if any clip drives an automation, so the engine can skip the whole
        evaluation pass on a project that uses none.
    */
    bool anyAutomation = false;
};

struct SampleProvider;

/** Builds a snapshot from a project tree. Runs on the message thread.
    Anything beyond the engine limits is dropped, and described in `warnings`.

    `pool` supplies the audio for audio channels. Optional, and null in every
    caller that has no audio to resolve - the offline renderer's own tests and
    every snapshot test predate audio entirely. A null pool leaves audio
    channels silent rather than reading files from the render path.
*/
EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings = nullptr,
                              SampleProvider* samples = nullptr,
                              SoundFontProvider* soundFonts = nullptr);

} // namespace dew
