#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <vector>

#include "../model/AutomationTargets.h"
#include "../model/ProjectSchema.h"
#include "Effects.h"

namespace dew
{

/** Limits on what the realtime engine will render.

    The audio thread must not allocate, so its voice pools and mix buffers are
    sized once in prepare(). That requires an upper bound. These are far above
    anything a prototype project reaches; buildSnapshot() clamps to them and
    reports what it dropped rather than silently truncating.
*/
inline constexpr int kMaxChannels          = 64;
inline constexpr int kMaxMixerTracks       = 32;
inline constexpr int kMaxVoicesPerChannel  = 16;

/** How many effect slots in the whole project can hold live DSP state.

    Each unit preallocates every effect type at once - a delay line, reverb
    tanks, a chorus - so it is a few hundred kilobytes. Capping the pool is what
    makes preallocation possible at all, and preallocation is what lets effect
    topology travel in the snapshot instead of needing a command queue and a
    pointer swap on the audio thread.
*/
inline constexpr int kMaxEffectUnits       = 32;
inline constexpr int kMaxAutomations       = 32;

enum class Waveform { sine, saw, square, triangle };

Waveform waveformFromString (const juce::String&);
juce::String waveformToString (Waveform);

struct OscSettings
{
    bool enabled = true;
    Waveform wave = Waveform::saw;
    int octave = 0;
    float detuneCents = 0.0f;
    float gain = 0.8f;
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
    bool anyEnabled = false;   ///< precomputed, so nothing downstream has to scan
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
    int id = 0;                ///< the document's effect id, which keys the DSP pool
    EffectType type = EffectType::filter;
    bool enabled = true;
    int unitIndex = -1;
    EffectParams params;
};

/** A chain of effect slots. Fixed size so a snapshot is trivially copyable and
    the audio thread never chases a pointer it did not allocate.
*/
struct EffectChainSnapshot
{
    std::array<EffectSnapshot, kMaxEffectsPerChain> slots;
    int numSlots = 0;
};

struct ChannelSnapshot
{
    int id = 0;
    int mixerTrackIndex = 0;   ///< resolved to an index, so the audio thread never searches
    int basePitch = 60;
    float volume = 0.8f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    OscBankSnapshot osc;
    AmpSettings amp;
    EffectChainSnapshot effects;
};

struct NoteSnapshot
{
    int channelIndex = -1;     ///< resolved; -1 means the note referenced a missing channel
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
    volume, pan, gain,
    cutoff, resonance, mix, roomSize, damping, width,
    delayMs, feedback, drive, outputGain, rate, depth,
    lowGainDb, midGainDb, midFreq, highGainDb
};

struct AutomationPointSnapshot
{
    double step = 0.0;
    float value = 0.0f;    ///< 0..1
    float curve = 0.0f;
};

/** One automation definition, with its target resolved to indices. */
struct AutomationSnapshot
{
    AutomationScope scope = AutomationScope::channel;
    int targetIndex = -1;      ///< channel index or mixer track index; -1 for master
    int slotIndex = -1;        ///< effect slot in the chain, -1 when not an effect
    AutomationParam param = AutomationParam::none;
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool logarithmic = false;
    std::vector<AutomationPointSnapshot> points;

    /** Value at a step, in the parameter's own units. */
    float valueAt (double step) const noexcept;
};

struct ClipSnapshot
{
    int patternIndex = -1;     ///< resolved
    int automationIndex = -1;  ///< resolved; >= 0 makes this an automation clip
    int startBar = 0;
    int lengthBars = 1;

    /** Resolved audibility of the playlist track this clip sits on. Folded in
        here so the sequencer never has to look a track up.
    */
    bool trackAudible = true;
};

struct MixerTrackSnapshot
{
    int id = 0;
    float gain = 0.8f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    EffectChainSnapshot effects;
};

/** Everything the audio thread needs to render, with every cross-reference
    already resolved to an array index. Built on the message thread from the
    ValueTree; read-only once published.
*/
struct EngineSnapshot
{
    double tempoBpm = 128.0;
    int stepsPerBeat = 4;
    int barsInSong = 16;

    std::vector<ChannelSnapshot> channels;
    std::vector<PatternSnapshot> patterns;
    std::vector<ClipSnapshot> clips;          ///< flattened across all playlist tracks
    std::vector<MixerTrackSnapshot> mixerTracks;
    std::vector<AutomationSnapshot> automations;

    float masterGain = 0.9f;
    EffectChainSnapshot masterEffects;

    // Solo is a property of the whole mixer, not of one track: one track soloed
    // silences every track that is not. Precomputed per scope so neither the
    // sequencer nor the mixer has to scan.
    bool anySolo = false;            ///< any mixer track soloed
    bool anyChannelSolo = false;     ///< any channel soloed
    bool anyPlaylistTrackSolo = false;

    /** Incremented on every build. The stress test uses it to tell snapshots
        apart; the engine uses it to notice that the document changed.
    */
    juce::uint64 generation = 0;

    static constexpr int beatsPerBar = 4;
    int stepsPerBar() const { return stepsPerBeat * beatsPerBar; }

    int patternIndexForId (int patternId) const;
    int channelIndexForId (int channelId) const;

    /** Length of the arrangement in steps: the end of the last clip. Zero when
        the playlist is empty.
    */
    int songLengthSteps() const;

    /** True when the snapshot has nothing that could make a sound. */
    bool isSilent() const;

    /** Whether a channel should sound, given mute and the mixer-wide solo state. */
    bool isChannelAudible (const ChannelSnapshot&) const noexcept;

    /** True if any chain anywhere has an enabled slot - lets the engine skip the
        whole stereo effect stage on a project that uses none.
    */
    bool anyEffects = false;

    /** True if any clip drives an automation, so the engine can skip the whole
        evaluation pass on a project that uses none.
    */
    bool anyAutomation = false;
};

/** Builds a snapshot from a project tree. Runs on the message thread.
    Anything beyond the engine limits is dropped, and described in `warnings`.
*/
EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings = nullptr);

} // namespace dew
