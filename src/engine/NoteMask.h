#pragma once

#include <cstdint>

namespace dew
{

/** Which pitches something is sounding, as a 128-bit set.

    Two words rather than a std::bitset because this crosses the thread
    boundary: it is published as a pair of std::atomic<uint64_t>, and a bitset
    is not lock-free at any width. 128 is MIDI's whole range, so a pitch can
    never fall outside it.
*/
struct NoteMask
{
    std::uint64_t low = 0;  ///< pitches 0..63
    std::uint64_t high = 0; ///< pitches 64..127

    void set (int pitch) noexcept
    {
        if (pitch < 0 || pitch >= 128)
            return;

        (pitch < 64 ? low : high) |= (std::uint64_t) 1 << (pitch & 63);
    }

    bool test (int pitch) const noexcept
    {
        if (pitch < 0 || pitch >= 128)
            return false;

        return ((pitch < 64 ? low : high) >> (pitch & 63) & 1) != 0;
    }

    bool any() const noexcept
    {
        return (low | high) != 0;
    }

    bool operator== (const NoteMask& other) const noexcept
    {
        return low == other.low && high == other.high;
    }

    bool operator!= (const NoteMask& other) const noexcept
    {
        return ! (*this == other);
    }
};

} // namespace dew
