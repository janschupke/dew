#pragma once

#include <atomic>

#include <juce_core/juce_core.h>

namespace dew
{

/** Raises `slot` to `peak` if `peak` is louder, and leaves it alone otherwise.

    Keep-the-loudest-since-the-last-read, so a meter cannot miss a transient just
    because it fell between two message-thread ticks. The reader clears the slot
    by exchanging zero into it.

    Written twice before this - once for the mixer meters and once for the input
    recorder - with the same algorithm and two different memory orderings. This
    is the stronger of the two: the release pairs with the reader's acquire
    exchange, which is what makes the samples that produced the peak visible to
    the thread that reads it.
*/
inline void atomicPeakMax (std::atomic<float>& slot, float peak) noexcept
{
    auto current = slot.load (std::memory_order_relaxed);

    while (peak > current
           && ! slot.compare_exchange_weak (current, peak, std::memory_order_release,
                                            std::memory_order_relaxed))
    {
    }
}

} // namespace dew
