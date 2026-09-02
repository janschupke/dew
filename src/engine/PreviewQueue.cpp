#include "PreviewQueue.h"

namespace dew
{

bool PreviewQueue::push (const PreviewEvent& event) noexcept
{
    const auto write = writeIndex.load (std::memory_order_relaxed);
    const auto read = readIndex.load (std::memory_order_acquire);

    // The producer must never move readIndex - that index belongs to the
    // consumer, and writing it here would be a race dressed up as a drop policy.
    if (write - read >= (unsigned int) capacity)
        return false;

    events[(size_t) (write & mask)] = event;
    writeIndex.store (write + 1, std::memory_order_release);

    return true;
}

bool PreviewQueue::pop (PreviewEvent& out) noexcept
{
    const auto read = readIndex.load (std::memory_order_relaxed);

    if (read == writeIndex.load (std::memory_order_acquire))
        return false;

    out = events[(size_t) (read & mask)];
    readIndex.store (read + 1, std::memory_order_release);
    return true;
}

} // namespace dew
