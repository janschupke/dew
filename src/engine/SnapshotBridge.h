#pragma once

#include <array>
#include <atomic>

#include "engine/EngineSnapshot.h"

namespace dew
{

/** Hands snapshots from the message thread to the audio thread.

    The audio thread must never allocate, lock, or touch the ValueTree, and the
    message thread must never block waiting for a block to finish.

    Three slots rotate between three roles - the reader's (`front`), the writer's
    (`back`), and the handover point (`ready`) - and ALL THREE indices live in a
    single atomic word, swapped with one compare-exchange. That is the whole
    point of the design: because each swap is a single atomic operation on the
    whole state, the three indices are always a permutation of {0, 1, 2}, so
    `front` and `back` can never name the same slot. The writer therefore cannot
    be writing what the reader is reading, and it is an invariant of the
    encoding rather than an argument about interleavings.

    An earlier version of this class kept the indices in separate atomics and
    reasoned that the writer would always exclude the slot being read. It was
    wrong: when publishes outrun the reader, the writer can observe a reader
    index that is two moves stale and pick the slot currently being read. The
    stress test in SnapshotBridgeTests found it at a rate of roughly two torn
    reads per ten thousand publishes, which is exactly the kind of defect that
    survives code review and then shows up as an occasional glitch.
*/
class SnapshotBridge
{
public:
    SnapshotBridge() = default;

    /** Message thread. Takes ownership; visible to the reader's next acquire(). */
    void publish (EngineSnapshot snapshot);

    /** Audio thread. Call once at the top of each block; the reference stays
        valid until the next call.
    */
    const EngineSnapshot& acquire() noexcept;

    /** The snapshot last acquired, without latching a newer one. */
    const EngineSnapshot& current() const noexcept
    {
        return slots[frontOf (state.load (std::memory_order_relaxed))];
    }

    /** True when a publish is waiting to be picked up. Diagnostics and tests. */
    bool hasPendingPublish() const noexcept
    {
        return (state.load (std::memory_order_acquire) & dirtyBit) != 0;
    }

private:
    // bits 0-1: front (reader)   bits 2-3: ready (handover)
    // bits 4-5: back  (writer)   bit  6  : a publish is waiting
    using State = std::uint_fast32_t;

    static constexpr State dirtyBit = 1u << 6;

    static constexpr size_t frontOf (State s) noexcept
    {
        return (size_t) (s & 0x3u);
    }
    static constexpr size_t readyOf (State s) noexcept
    {
        return (size_t) ((s >> 2) & 0x3u);
    }
    static constexpr size_t backOf (State s) noexcept
    {
        return (size_t) ((s >> 4) & 0x3u);
    }

    static constexpr State pack (size_t front, size_t ready, size_t back, bool dirty) noexcept
    {
        return (State) (front | (ready << 2) | (back << 4) | (dirty ? dirtyBit : 0u));
    }

    /** Writer's move: the slot it just filled becomes the handover point. */
    static constexpr State swapBackAndReady (State s) noexcept
    {
        return pack (frontOf (s), backOf (s), readyOf (s), true);
    }

    /** Reader's move: it takes the handover point and gives up what it held. */
    static constexpr State swapFrontAndReady (State s) noexcept
    {
        return pack (readyOf (s), frontOf (s), backOf (s), false);
    }

    std::array<EngineSnapshot, 3> slots;

    // front = 0, ready = 1, back = 2, nothing pending.
    std::atomic<State> state { pack (0, 1, 2, false) };

    JUCE_DECLARE_NON_COPYABLE (SnapshotBridge)
};

} // namespace dew
