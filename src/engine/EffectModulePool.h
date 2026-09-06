#pragma once

#include <deque>
#include <memory>
#include <vector>

#include "engine/Module.h"
#include "model/Constants.h"

namespace dew
{

/** Effect DSP state, keyed on (pool index, effect type).

    THE INVARIANT, and it is a correctness argument rather than thrift:
    a module, once made, is NEVER destroyed while the pool lives.

    SnapshotBridge has no acknowledgement path. The message thread publishes a
    snapshot and never learns when the audio thread has finished reading an
    older one - that is the whole design, and it is why publishing is a single
    CAS with no waiting. A snapshot carries raw pointers to the modules its
    slots use, so anything a snapshot can point at has to outlive every
    snapshot, which means the engine.

    So: reusing a slot for another effect is a reset(), never a destruction.
    An "optimisation" that frees a module nobody is using any more hands the
    audio thread a dangling pointer, in a window no test can reliably catch and
    on a thread that cannot report it. This is the same rule SynthVoice already
    follows for its `const Wavetable*`.

    What DID change is when a module is made. Every unit used to hold every
    effect type at once, all constructed and prepared up front: about 0.45MB
    each, thirty-two of them, so roughly 15MB per AudioEngine at 44.1kHz and
    30MB at 96k, whether the project used a single effect or none. The offline
    renderer builds a fresh engine per render, and paid it every time. Modules
    are now made on the message thread on first use, so a project materialises
    what it actually uses.
*/
class EffectModulePool
{
public:
    /** Message thread. Prepares everything already made, and fixes the rate and
        block size anything made later will be prepared with. */
    void prepare (double sampleRate, int maximumBlockSize);

    void releaseResources();

    /** Message thread ONLY - it allocates. Returns a pointer good for the
        pool's lifetime, making and preparing the module on first use. */
    EffectModule* acquire (int poolIndex, EffectType);

    /** AUDIO THREAD. Silences every module that exists.

        A reset is what a slot reused as a different effect already gets, for
        the reason at runChain: a reverb tail read out through a delay line is
        noise. This is the same call over the whole pool, and it is what makes a
        panic silence the tails rather than only the voices - a two second
        reverb outlives every note that fed it.

        Allocation-free by construction: it walks what is already there and
        never makes one. Nothing is destroyed, which is the invariant this whole
        class exists to hold - a snapshot may still point at any of them.
    */
    void resetAll() noexcept;

    /** How many modules exist. For the test that pins the laziness. */
    int materialisedCount() const noexcept
    {
        return (int) owned.size();
    }

private:
    struct Entry
    {
        int poolIndex;
        EffectType type;
        EffectModule* module;
    };

    /** A deque, not a vector: nothing an entry points at may move, and it is
        easier to state the invariant when nothing moves at all. */
    std::deque<std::unique_ptr<EffectModule>> owned;
    std::vector<Entry> index;

    double sampleRate = kDefaultSampleRate;
    int blockSize = kDefaultBlockSize;
    bool prepared = false;
};

} // namespace dew
