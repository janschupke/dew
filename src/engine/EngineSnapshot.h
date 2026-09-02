#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

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

enum class Waveform { sine, saw, square, triangle };

Waveform waveformFromString (const juce::String&);
juce::String waveformToString (Waveform);

struct OscSettings
{
    Waveform wave = Waveform::saw;
    int octave = 0;
    float detuneCents = 0.0f;
    float gain = 0.8f;
};

struct AmpSettings
{
    float attack = 0.005f, decay = 0.12f, sustain = 0.7f, release = 0.15f;
};

struct ChannelSnapshot
{
    int id = 0;
    int mixerTrackIndex = 0;   ///< resolved to an index, so the audio thread never searches
    int basePitch = 60;
    float volume = 0.8f;
    float pan = 0.0f;
    bool muted = false;
    OscSettings osc;
    AmpSettings amp;
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

struct ClipSnapshot
{
    int patternIndex = -1;     ///< resolved
    int startBar = 0;
    int lengthBars = 1;
};

struct MixerTrackSnapshot
{
    int id = 0;
    float gain = 0.8f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
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

    float masterGain = 0.9f;
    bool anySolo = false;                     ///< precomputed: solo changes every track's audibility

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
};

/** Builds a snapshot from a project tree. Runs on the message thread.
    Anything beyond the engine limits is dropped, and described in `warnings`.
*/
EngineSnapshot buildSnapshot (const juce::ValueTree& project, juce::StringArray* warnings = nullptr);

} // namespace dew
