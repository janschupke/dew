#pragma once

#include <array>

#include "engine/SynthVoice.h"

namespace dew
{

/** A polyphonic instrument: one oscillator bank, several voices of it.

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

    void noteOn (int pitch, float velocity, const OscBankSnapshot&, const AmpSettings&,
                 int durationSamples, int startOffset = 0);

    /** Releases every voice sounding this pitch. Used for held preview notes,
        which have no duration to run out - the user decides when they end.
    */
    void noteOff (int pitch) noexcept;

    /** Releases everything, without cutting the tails. */
    void allNotesOff() noexcept;

    /** Adds the channel's output into `mono`, and its panned oscillators into
        the optional pair.

        `bendSemitones` and `modulation` are this channel's continuous
        controllers, applied to every sounding voice before the block is
        rendered. They default to nothing, so a caller that has no controller
        renders exactly what it always did.

        `live` is the channel's oscillator bank as it stands THIS block, with
        any automation already folded in. Only the wavetable position is read
        from it - everything else a voice needs was latched at note-on. Null is
        allowed and means "nothing has moved", which is what every caller that
        predates automated positions passes.

        `panLeft` and `panRight` take the oscillators an LFO is sweeping across
        the field; everything else goes into `mono`. Both default to nothing, so
        a caller that predates LFOs renders exactly what it always did - see
        SynthVoice::renderAdd.
    */
    void renderAdd (float* mono, int numSamples, float bendSemitones = 0.0f,
                    float modulation = 0.0f, const OscBankSnapshot* live = nullptr,
                    float* panLeft = nullptr, float* panRight = nullptr) noexcept;

    /** Whether any voice sounding right now has an oscillator its LFO is
        sweeping across the field.

        Asked AFTER this block's note-ons and before its render, because a note
        starting in this very block may be the one that needs a side - and a
        voice that has already ended must stop asking for one.
    */
    bool hasPannedVoices() const noexcept;

    int countActiveVoices() const noexcept;

private:
    std::array<SynthVoice, kMaxVoicesPerChannel> voices;
};

} // namespace dew
