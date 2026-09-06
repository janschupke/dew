#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/EngineSnapshot.h"

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

    /** The same, with mute taken from an automation override. */
    static bool isAudible (const EngineSnapshot&, const MixerTrackSnapshot&,
                           bool muteOverride) noexcept;

    /** Constant-power pan: -1 hard left, 0 centre, +1 hard right. Equal power
        rather than linear, so panning does not dip in level at the centre.
    */
    static void panGains (float pan, float& leftGain, float& rightGain) noexcept;

    /** Where a MODULATED source sits, as a pair of gains.

        Not the constant-power law above, and the difference is a decision
        rather than thrift. That one costs a centred source 3dB and trackGains
        pays it back with a sqrt2 - so a source panned through it at dead centre
        comes out at cos(pi/4), not at 1. For a static placement that is right
        and the compensation happens once, downstream.

        Here it would be wrong, because this pan is a MODULATION around centre:
        an oscillator whose LFO moves its pitch and not its position would be
        quietly attenuated the moment the LFO was switched on, and a depth of
        zero would not mean "unchanged". So:

            pan  0 -> (1, 1)      pan +1 -> (0, 1)      pan -1 -> (1, 0)

        Exactly 1.0f on both sides at the centre, by construction rather than by
        an equality test. The cost is a 6dB dip at the extremes rather than 3,
        which on a continuous sweep reads as part of the movement rather than as
        a level jump - and the mixer keeps the constant-power law where a static
        placement is actually being made.
    */
    static void modulationPanGains (float pan, float& leftGain, float& rightGain) noexcept;

    /** Adds a stereo source into a stereo pair, applying gain and pan.

        A BALANCE law, not a rotation: at pan -1 the source's right side is
        attenuated away rather than folded into the left. That is the same rule a
        mono source has always followed - it is only visible now that an
        instrument can hand over two genuinely different sides.

        The source was mono until instruments became stereo. Passing the same
        pointer twice is exactly what the mono form did, bit for bit: with
        `sourceLeft == sourceRight` these are the identical two expressions on
        identical values.
    */
    static void addPanned (const float* sourceLeft, const float* sourceRight, int numSamples,
                           float gain, float pan, float* left, float* right) noexcept;

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
    struct TrackGains
    {
        float left, right, meter;
    };

    static TrackGains trackGains (float pan, float gain) noexcept;
};

} // namespace dew
