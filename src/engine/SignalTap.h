#pragma once

#include <array>
#include <atomic>

#include <juce_core/juce_core.h>
#include "../model/Constants.h"

namespace dew
{

/** Hands the master output back from the audio thread to the display.

    The first bridge in dew that runs in this direction, and it wants the
    opposite contract from the other two. PreviewQueue delivers every event,
    because both halves of a click matter. SnapshotBridge hands over whole
    states, because half a state is nonsense. A visualiser wants neither: it
    wants the most recent couple of thousand samples, now, and every sample
    older than that is worthless. Delivering all of them in order would mean a
    display stalled behind an open menu comes back and redraws a third of a
    second of the past.

    So: a ring that overwrites its oldest samples without asking, and one
    monotonic count of everything ever written. The writer never waits, never
    drops, and never reads anything the reader owns. The reader asks for the
    newest N, works out from the count which ABSOLUTE sample indices those are,
    copies them, and then asks the count again - if the writer has since moved
    on by more than the ring holds, the samples it copied are no longer the ones
    it thought it had, and it says so rather than drawing a seam. The ring is
    four times the largest window, so a refusal means the message thread was
    preempted for a tenth of a second, which is exactly when the frame it
    already had is the right answer.

    The slots are std::atomic<float> rather than a plain float array, and that
    is the whole safety argument rather than a decoration. "Copy it and check
    afterwards" over a plain array is a data race - undefined behaviour, not
    merely a value that might turn out stale - and a compiler is entitled to act
    on that. A relaxed atomic load or store of a float is a single instruction
    on every machine dew runs on; the only thing given up is vectorising a copy
    that has to be a per-sample loop anyway, because the downmix happens here.

    Downmixed to mono on the audio thread on purpose. The display is 26 pixels
    tall and shows the bus as one signal, so carrying stereo would double the
    ring, double the reader's work, and buy a distinction nobody can see at that
    size. The mean of the pair rather than the sum, so a centred source keeps its
    amplitude. One consequence is worth saying out loud: a pair that is fully out
    of phase reads as silence here. That is what a mono listener hears, and a
    display that hid it would be lying.
*/
class SignalTap
{
public:
    /** Four times the largest window a reader can ask for. The slack is what
        makes a refused read mean "the UI was stalled" rather than "two threads
        were running".
    */
    static constexpr int capacity = 8192;
    static constexpr int maxWindow = 2048;

    static_assert ((capacity & (capacity - 1)) == 0, "capacity must be a power of two");
    static_assert (capacity >= 4 * maxWindow, "the ring must be much larger than a window");

    /** Audio thread. Both pointers must hold `numSamples` samples. */
    void write (const float* left, const float* right, int numSamples) noexcept;

    /** Message thread. Fills `destination` with the newest `numSamples`, oldest
        first.

        Returns false when the writer overtook the copy; the caller should keep
        the frame it already had. Before `numSamples` have ever been written the
        missing history reads as silence and the call still succeeds, so a
        display that has just started shows a growing trace rather than nothing.
    */
    bool readLatest (float* destination, int numSamples) const noexcept;

    /** Set from AudioEngine::prepare. The rate lives here rather than being read
        off the engine, so a frame is always drawn at the rate it was captured
        at - a reader that fetched the samples and the rate from two different
        objects would plot the wrong span for one frame every device change.
    */
    void setSampleRate (double) noexcept;
    double getSampleRate() const noexcept;

    /** Total samples ever written. A display uses it to tell "silence is
        arriving" from "nothing is arriving at all", which are identical in the
        samples and must not be identical on screen.
    */
    juce::int64 getWriteCount() const noexcept;

    /** Message thread, between device sessions. */
    void reset() noexcept;

private:
    static constexpr int mask = capacity - 1;

    std::array<std::atomic<float>, capacity> samples {};
    std::atomic<juce::int64> writeCount { 0 };
    std::atomic<double> sampleRate { kDefaultSampleRate };
};

} // namespace dew
