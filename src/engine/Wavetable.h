#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <functional>
#include <vector>

namespace dew
{

/** How many morph frames one wavetable carries.

    Sixteen rather than the 256 a commercial synth ships: every frame is stored
    band-limited at every mip level, so a frame costs real memory, and the
    position control crossfades between neighbours anyway. Sixteen is enough
    that the crossfade never has to span two unrelated timbres.
*/
inline constexpr int kWavetableFrames = 16;

/** Samples in one frame at mip 0. A power of two, because every mip is built
    by an inverse FFT of that size.
*/
inline constexpr int kWavetableSize = 2048;

/** How many band-limited versions of each frame are stored.

    Mip `m` holds harmonics 1..(1024 >> m), which is the most a note whose phase
    increment is `dt` can carry without folding: the voice picks the first mip
    whose harmonic limit clears Nyquist. Eleven levels is what it takes for the
    top of the MIDI range to reach a single harmonic, i.e. a plain sine, which
    cannot alias at any pitch.

    Each mip is stored at HALF the length of the one above it rather than at
    full length. Storing all eleven at 2048 would cost 1.4MB a table; halving
    costs 264KB, and a mip that only carries N harmonics has nothing to say at
    more than 2N points anyway.
*/
inline constexpr int kWavetableMips = 11;

/** Length of one frame at mip `m`, floored at 8 so the smallest levels still
    have somewhere to put their one or two harmonics.
*/
constexpr int wavetableMipSize (int mip) noexcept
{
    const auto m = mip < 0 ? 0 : (mip >= kWavetableMips ? kWavetableMips - 1 : mip);
    const auto n = kWavetableSize >> m;
    return n < 8 ? 8 : n;
}

/** Highest harmonic present at mip `m`. */
constexpr int wavetableMipHarmonics (int mip) noexcept
{
    const auto m = mip < 0 ? 0 : (mip >= kWavetableMips ? kWavetableMips - 1 : mip);
    const auto h = (kWavetableSize / 2) >> m;
    return h < 1 ? 1 : h;
}

/** The mip a note with phase increment `dt` must read to stay band-limited.

    Solves (1024 >> m) * dt <= 0.5 for m. Called once per block rather than per
    sample: a bend moves `dt`, but not far enough within one block to matter.
*/
int wavetableMipFor (double phaseIncrement) noexcept;

/** A bank of morph frames, each band-limited at every mip level.

    Generated from a harmonic spectrum rather than loaded from a file, so the
    whole factory set is a formula: no binary data in the repo, no disk I/O, and
    an offline render of a project made on one machine is sample-identical on
    another. That reproducibility is a tested contract here, not a nicety.

    Built once on the message thread and never mutated. The audio thread only
    ever reads, through a pointer it did not allocate and does not own - which
    is safe precisely because a Wavetable outlives every voice that can see it.
*/
class Wavetable
{
public:
    /** Fills `amplitudes[k]` for k in 1..kWavetableSize/2 with the amplitude of
        the k'th harmonic of frame `frame`. Index 0 is DC and is ignored: a
        wavetable frame with an offset would push a click through the envelope.

        Sine phase throughout. A saw built from cosines is not a saw, so the
        convention is pinned by a test rather than left to the FFT's.
    */
    using FrameSpectrum = std::function<void (int frame, std::vector<float>& amplitudes)>;

    Wavetable (juce::String name, juce::String displayName, const FrameSpectrum&);

    /** One frame's samples at one mip, with a wrapped guard sample at [size],
        so interpolation never needs a modulo.
    */
    const float* frameData (int frame, int mip) const noexcept;

    /** The wavetable at a morph position, crossfading the two frames it falls
        between. `phase` is in [0, 1).
    */
    float at (float position, int mip, double phase) const noexcept;

    /** The stored name, which is what a .dew carries. */
    const juce::String& getName() const noexcept
    {
        return name;
    }

    /** What the panel's dropdown shows. */
    const juce::String& getDisplayName() const noexcept
    {
        return displayName;
    }

private:
    float sampleAt (int frame, int mip, double phase) const noexcept;

    juce::String name, displayName;

    std::vector<float> storage;
    std::array<int, kWavetableMips> mipOffset {};
    int frameStride = 0;
};

/** How many factory wavetables there are. */
int wavetableCount() noexcept;

/** The factory wavetable at `index`, clamped into range.

    The bank is built on first call. The audio thread must never be the caller
    that builds it - generation allocates and runs an FFT - so SynthChannel's
    prepare() touches it, which is always the message thread.
*/
const Wavetable& wavetableAt (int index) noexcept;

/** The index of a stored name, or -1 when nothing matches.

    A document stores the NAME, not the index. An index in the file would
    silently repoint every project the day a table is inserted into the list.
*/
int wavetableIndexFor (const juce::String& name) noexcept;

} // namespace dew
