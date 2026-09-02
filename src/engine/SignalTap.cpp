#include "SignalTap.h"

namespace dew
{

void SignalTap::write (const float* left, const float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    // More than a ring in one block means everything before the tail is about to
    // be overwritten anyway. Skipping it draws the same picture for less work,
    // and keeps the count exact either way.
    const auto skip = juce::jmax (0, numSamples - capacity);
    left += skip;
    right += skip;

    const auto kept = numSamples - skip;
    const auto count = writeCount.load (std::memory_order_relaxed);

    for (int i = 0; i < kept; ++i)
        samples[(size_t) ((count + skip + i) & mask)].store (0.5f * (left[i] + right[i]),
                                                            std::memory_order_relaxed);

    // The one release in the class: every store above is visible to any reader
    // that observes this count.
    writeCount.store (count + numSamples, std::memory_order_release);
}

bool SignalTap::readLatest (float* destination, int numSamples) const noexcept
{
    if (destination == nullptr || numSamples <= 0 || numSamples > capacity)
        return false;

    const auto end = writeCount.load (std::memory_order_acquire);
    const auto start = end - numSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto index = start + i;

        destination[i] = index < 0
                             ? 0.0f
                             : samples[(size_t) (index & mask)].load (std::memory_order_relaxed);
    }

    // A fence, not an acquire load, because what has to be prevented is the read
    // BELOW being hoisted above the copy. A check answered with a count fetched
    // before the samples were is a check that always passes, which is no check.
    std::atomic_thread_fence (std::memory_order_acquire);

    const auto endNow = writeCount.load (std::memory_order_relaxed);

    // Intact exactly when the oldest sample copied is still one the ring holds.
    return endNow - start <= (juce::int64) capacity;
}

void SignalTap::setSampleRate (double newRate) noexcept
{
    sampleRate.store (newRate > 0.0 ? newRate : kDefaultSampleRate, std::memory_order_relaxed);
}

double SignalTap::getSampleRate() const noexcept
{
    return sampleRate.load (std::memory_order_relaxed);
}

juce::int64 SignalTap::getWriteCount() const noexcept
{
    return writeCount.load (std::memory_order_acquire);
}

void SignalTap::reset() noexcept
{
    for (auto& sample : samples)
        sample.store (0.0f, std::memory_order_relaxed);

    writeCount.store (0, std::memory_order_release);
}

} // namespace dew
