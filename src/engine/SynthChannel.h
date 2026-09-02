#pragma once

#include <array>

#include "SynthVoice.h"

namespace dew
{

/** A polyphonic instrument: one oscillator design, several voices of it.

    Voices are preallocated in prepare() because the audio thread cannot
    allocate. When they are all busy the oldest is stolen, which is the least
    surprising choice for a step sequencer - the note about to be cut is the one
    that has been ringing longest.
*/
class SynthChannel
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    void noteOn (int pitch, float velocity, const OscSettings&, const AmpSettings&, int durationSamples);

    /** Releases every voice sounding this pitch. Used for held preview notes,
        which have no duration to run out - the user decides when they end.
    */
    void noteOff (int pitch) noexcept;

    /** Releases everything, without cutting the tails. */
    void allNotesOff() noexcept;

    /** Adds the channel's mono output into `buffer`. */
    void renderAdd (float* buffer, int numSamples) noexcept;

    int countActiveVoices() const noexcept;

private:
    std::array<SynthVoice, kMaxVoicesPerChannel> voices;
};

} // namespace dew
