#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "EngineSnapshot.h"

namespace dew
{

/** Sums channels into mixer tracks and mixer tracks into the master.

    Solo is resolved against the whole snapshot rather than per track, because
    one track being soloed silences every track that is not - a decision that
    cannot be made by looking at a track alone.
*/
struct MixerBus
{
    /** True if a track should be heard, given the solo state of the whole mixer. */
    static bool isAudible (const EngineSnapshot&, const MixerTrackSnapshot&) noexcept;

    /** Constant-power pan: -1 hard left, 0 centre, +1 hard right. Equal power
        rather than linear, so panning does not dip in level at the centre.
    */
    static void panGains (float pan, float& leftGain, float& rightGain) noexcept;

    /** Adds `mono` into a stereo pair, applying gain and pan. */
    static void addPanned (const float* mono, int numSamples, float gain, float pan,
                           float* left, float* right) noexcept;
};

} // namespace dew
