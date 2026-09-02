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

    /** What a mixer track contributes to the master, and what its meter shows.

        The sqrt2 is the constant-power compensation: panGains costs a centred
        signal 3dB, which is right when summing many channels into one track and
        wrong when a track is the whole signal. It was written out three times in
        the master sum - twice for the audio and once for the meter - and the
        meter's copy has to agree with the audio's or the fader and the bar beside
        it disagree.

        `meter` is deliberately not max(left, right) of the returned pair: it is
        the pre-fader buffer's scale, because the buffers hold the track before
        its gain is applied.
    */
    struct TrackGains { float left, right, meter; };

    static TrackGains trackGains (float pan, float gain) noexcept;
};

} // namespace dew
