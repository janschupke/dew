#pragma once

#include <array>
#include <atomic>

namespace dew
{

/** One note the user asked to hear directly, outside the sequencer. */
struct PreviewEvent
{
    enum class Kind { noteOn, noteOff, allOff };

    Kind kind = Kind::noteOn;
    int channelIndex = 0;
    int pitch = 60;
    float velocity = 0.8f;
};

/** Hands preview notes from the message thread to the audio thread.

    Single producer, single consumer, fixed capacity, no allocation and no
    locking - the same constraints the snapshot bridge works under, for the same
    reason.

    A ring rather than a "latest wins" atomic, because both halves of a click
    matter: press and release can land inside one 5.8ms block, and with
    latest-wins the release would overwrite the press and the key would make no
    sound at all. Every event is delivered, in order.

    A full queue refuses the new event and says so. Dropping the OLDEST would be
    friendlier, but the producer would have to move the read index, and the read
    index belongs to the consumer - that is a data race, not a policy choice. At
    64 events per block it takes a pointer moving faster than the audio thread
    renders to overflow at all, and the caller is told when it happens.
*/
class PreviewQueue
{
public:
    /** Deliberately larger than any plausible number of events in one block, so
        overflow means something has gone wrong rather than being routine.
    */
    static constexpr int capacity = 64;

    /** Message thread. Returns false if an event had to be dropped to fit. */
    bool push (const PreviewEvent& event) noexcept;

    /** Audio thread. Returns false when the queue is empty. */
    bool pop (PreviewEvent& out) noexcept;

    bool isEmpty() const noexcept
    {
        return readIndex.load (std::memory_order_acquire) == writeIndex.load (std::memory_order_acquire);
    }

private:
    static constexpr int mask = capacity - 1;
    static_assert ((capacity & mask) == 0, "capacity must be a power of two");

    std::array<PreviewEvent, capacity> events {};

    // The writer owns writeIndex, the reader owns readIndex; each publishes with
    // release and observes the other with acquire.
    std::atomic<unsigned int> writeIndex { 0 };
    std::atomic<unsigned int> readIndex { 0 };
};

} // namespace dew
