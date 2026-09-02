#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace dew::RenderPost
{

/** What a render does to its buffer after the engine has finished with it.

    Free functions with no state, like Sequencer and MixerBus: each one is a
    transformation of a buffer that can be tested on its own, without an engine,
    a project or a file.

    The ORDER these are applied in is not arbitrary, and OfflineRenderer applies
    them in this one: fades, then normalize, then - only at the writer - dither.

    Fades before normalize, because what the user asked for is that the FILE
    peaks at the target. Normalizing first and fading second leaves a file
    quieter than asked for whenever the peak happens to sit inside a fade.
*/

/** Ramps the first and last stretch of the buffer in and out.

    A fade-in is what stops a range that starts mid-note from opening on a
    click; a fade-out does the same for a tail cut short. Each is clamped to
    half the buffer so that two long fades cannot overlap and cancel.
*/
void applyFades (juce::AudioBuffer<float>& buffer,
                 double sampleRate,
                 double fadeInSeconds,
                 double fadeOutSeconds) noexcept;

/** Scales the buffer so its peak sits at `targetPeak` (linear, 0..1).

    Returns the gain it applied, in dB, which is 0 when it did nothing.

    A silent buffer is left alone rather than divided by nothing, and the boost
    is capped: without a cap, "normalize" on a near-silent render is a command
    to amplify its own noise floor by however many dB it takes.
*/
float normalize (juce::AudioBuffer<float>& buffer,
                 float targetPeak,
                 float maxBoostDb = 40.0f) noexcept;

/** Adds triangular (TPDF) noise at +/-1 LSB of `bitDepth`.

    JUCE does not dither. AudioFormatWriter::convertFloatsToInts is a plain
    round-and-clip, so a 16-bit export currently gets truncation distortion on
    quiet passages - which is correlated with the signal, and therefore audible
    in a way that noise of the same size is not.

    A no-op above `maxDitheredBitDepth`, where the truncation is already below
    anything the material contains, and a no-op for float destinations, which do
    not truncate at all.

    `seed` is explicit and fixed by default, because a render has to be
    reproducible: two renders of the same project must produce the same file, or
    the save-and-reload test cannot mean anything. This is the one part of the
    render that is not a pure function of the input, so it is made one by hand.
*/
void dither (juce::AudioBuffer<float>& buffer, int bitDepth, juce::int64 seed) noexcept;

/** Above this, truncation is quieter than anything the engine produces. */
inline constexpr int maxDitheredBitDepth = 16;

} // namespace dew::RenderPost
