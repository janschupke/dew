#include "engine/SnapshotBridge.h"

namespace dew
{

void SnapshotBridge::publish (EngineSnapshot snapshot)
{
    // `back` belongs to the writer: only publish() ever changes it, so reading
    // it without synchronisation is safe, and the reader never names it.
    auto current = state.load (std::memory_order_acquire);

    slots[backOf (current)] = std::move (snapshot);

    // Release the write above, then hand the slot over. If the reader swaps in
    // between, the compare-exchange retries against its new state.
    while (! state.compare_exchange_weak (current,
                                          swapBackAndReady (current),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire))
    {
        // A reader swap only moves front and ready; `back` is unchanged, so the
        // snapshot just written is still the one being handed over.
    }
}

const EngineSnapshot& SnapshotBridge::acquire() noexcept
{
    auto current = state.load (std::memory_order_acquire);

    // Nothing new: keep reading the slot already held, without touching state.
    if ((current & dirtyBit) == 0)
        return slots[frontOf (current)];

    while (! state.compare_exchange_weak (current,
                                          swapFrontAndReady (current),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire))
    {
        // A publish landed during the swap. If it is still pending, take that
        // one instead; if the flag has cleared there is nothing left to take.
        if ((current & dirtyBit) == 0)
            return slots[frontOf (current)];
    }

    return slots[readyOf (current)];
}

} // namespace dew
