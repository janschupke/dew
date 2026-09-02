#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <bitset>

#include "AudioEngine.h"

namespace dew
{

/** Turns MIDI messages into engine calls.

    Deliberately knows nothing about devices. It takes a juce::MidiMessage and
    calls the engine, which is what makes every rule below testable by
    constructing a message - no hardware, no thread, no device list. All the
    behaviour lives here; MidiInputHost above it only owns the ports.

    Called from the OS MIDI thread. It therefore never touches EditorState, the
    ValueTree, or any component: the channel it plays is pushed down to it as an
    index by the message thread, and everything else it needs is its own.
*/
class MidiRouter
{
public:
    explicit MidiRouter (AudioEngine&);

    /** MIDI thread. The whole of the translation. */
    void handleMessage (const juce::MidiMessage&) noexcept;

    /** MESSAGE THREAD. Releases everything sounding and returns the controllers
        to rest - when a device goes away, or the target channel changes.

        The MIDI thread's own version of this is panic(), and the two are
        deliberately not one function. panic() pushes to the MIDI ring and
        clears the note bookkeeping in place, and both of those belong to the
        MIDI thread: pushing to that ring from here would give it the second
        producer the two-ring design exists to avoid, and clearing a bitset the
        MIDI thread is reading is a plain data race.

        So this goes through the message thread's OWN ring, and asks the MIDI
        thread to clear its own bookkeeping next time it wakes. panic() is
        private so that this is the only one callable from outside.
    */
    void reset() noexcept;

    // --- configuration, written by the message thread ------------------------
    /** Which engine channel plays. Changing it releases what the old one was
        holding, and unbends it - a channel left bent stays bent forever.
    */
    void setTargetChannel (int channelIndex) noexcept;
    int getTargetChannel() const noexcept  { return targetChannel.load (std::memory_order_relaxed); }

    /** 0 for omni, or 1..16 for a single MIDI channel. */
    void setChannelFilter (int midiChannel) noexcept;
    int getChannelFilter() const noexcept  { return channelFilter.load (std::memory_order_relaxed); }

    /** Semitones added to every incoming note. */
    void setTranspose (int semitones) noexcept;
    int getTranspose() const noexcept  { return transpose.load (std::memory_order_relaxed); }

    // --- what arrived, for the UI to show ------------------------------------
    /** Bumped on every message that passes the filter, so a panel can show that
        something is arriving without the MIDI thread touching a component.
    */
    juce::uint32 getActivityCount() const noexcept { return activity.load (std::memory_order_relaxed); }

    /** The last note that played, packed so it can be read in one atomic load:
        pitch in bits 0-7, velocity 8-15, MIDI channel 16-23. -1 when nothing
        has arrived yet.
    */
    int getLastNote() const noexcept  { return lastNote.load (std::memory_order_relaxed); }

    static int packNote (int pitch, int velocity, int midiChannel) noexcept;

    /** The MIDI default. A wheel is ±2 semitones unless told otherwise, and
        nothing in dew tells it otherwise yet.
    */
    static constexpr float bendRangeSemitones = 2.0f;

    /** Below this a sustain pedal is up. The spec's own half-way point. */
    static constexpr int sustainThreshold = 64;

    static constexpr int omni = 0;
    static constexpr int maxTranspose = 24;

private:
    /** MIDI THREAD ONLY. What CC 120 and CC 123 do.

        Private on purpose: it writes the MIDI ring and the note bookkeeping
        directly, so calling it from anywhere but handleMessage would be a data
        race. Callers outside want reset().
    */
    void panic() noexcept;

    void noteOn (int pitch, float velocity) noexcept;
    void noteOff (int pitch) noexcept;

    /** Applies transpose and reports whether the result is playable. A note
        pushed off either end is dropped, never wrapped: wrapping would answer a
        key at the top of the keyboard with a note at the bottom.
    */
    bool transposed (int pitch, int& out) const noexcept;

    AudioEngine& engine;

    std::atomic<int> targetChannel { 0 };
    std::atomic<int> channelFilter { omni };
    std::atomic<int> transpose { 0 };

    std::atomic<juce::uint32> activity { 0 };
    std::atomic<int> lastNote { -1 };

    /** Set by the message thread, consumed by the MIDI thread at the top of
        handleMessage. The handshake that lets reset() clear the state below
        without writing to it.
    */
    std::atomic<bool> clearRequested { false };

    // Touched ONLY by the MIDI thread, which is the only caller of
    // handleMessage. Nothing else may write these.
    std::bitset<128> sounding;
    std::bitset<128> heldBySustain;
    bool sustainDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRouter)
};

} // namespace dew
