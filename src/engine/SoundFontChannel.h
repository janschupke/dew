#pragma once

#include <array>

#include "engine/SoundFontVoice.h"

namespace dew
{

/** How many regions of one soundfont a channel can have sounding at once.

    Thirty-two rather than the synth's sixteen, and not for generosity: one
    note-on starts a voice per MATCHING region, and a stereo piano with two
    velocity layers matches four. Sixteen would make that a four-note chord.
*/
inline constexpr int kMaxSoundFontVoicesPerChannel = 32;

/** A soundfont channel's voices.

    Preallocated in prepare() because the audio thread cannot allocate, and the
    oldest is stolen when they are all busy - the same rule SynthChannel
    follows, and the least surprising one for a step sequencer.
*/
class SoundFontChannel
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Starts every region of `preset` that answers to this key and velocity.

        Several, usually: velocity layers and the two halves of a stereo pair
        are separate regions. A region carrying an exclusive class silences the
        sounding voices that share it before it starts, which is how a closed
        hi-hat cuts an open one.
    */
    void noteOn (const SoundFontData& font, const SoundFontPreset& preset, int pitch,
                 float velocity, const SoundFontSettings&, int durationSamples,
                 int startOffset) noexcept;

    /** Releases every voice sounding this pitch. */
    void noteOff (int pitch) noexcept;

    void allNotesOff() noexcept;

    /** ADDS the channel's stereo output. */
    void renderAdd (float* left, float* right, int numSamples) noexcept;

    int countActiveVoices() const noexcept;

private:
    SoundFontVoice& voiceToUse() noexcept;

    std::array<SoundFontVoice, kMaxSoundFontVoicesPerChannel> voices;
};

} // namespace dew
