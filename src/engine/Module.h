#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "model/ModuleCatalog.h"

namespace dew
{

/** A slot's parameter values, already clamped, in the order its type declares.

    A view, not a copy: the values live in the snapshot the audio thread is
    already holding, and copying them per slot per block is what this whole
    layer exists to stop doing.

    A choice - the filter's mode - arrives as a float holding its index, the way
    a discrete parameter is a float in every plugin API. That keeps the
    interface one shape rather than one shape plus an exception.
*/
class ParamBlock
{
public:
    ParamBlock (const float* v, int n) noexcept
        : values (v)
        , count (n)
    {
    }

    float operator[] (int i) const noexcept
    {
        jassert (i >= 0 && i < count);
        return values[i];
    }

    /** A choice parameter's index, rounded and clamped to the set. */
    int choice (int i, int numChoices) const noexcept
    {
        return juce::jlimit (0, numChoices - 1, (int) ((*this)[i] + 0.5f));
    }

    int size() const noexcept
    {
        return count;
    }

private:
    const float* values;
    int count;
};

/** A stereo pair to work on, in place.

    Two bare pointers rather than a juce::AudioBuffer because the engine's
    scratch IS two pointers into a bigger buffer, and wrapping them every block
    would be a constructor per slot per block for nothing.
*/
struct StereoView
{
    float* left;
    float* right;
    int numSamples;
};

/** One effect, as a module.

    Deliberately AudioProcessor-shaped. prepare / reset / process /
    releaseResources map one-to-one onto prepareToPlay / reset / processBlock /
    releaseResources, so wrapping a dew effect as a juce::AudioProcessor later
    is a wrapper rather than a rewrite - and none of this depends on
    juce_audio_processors today.

    What is deliberately ABSENT is state serialisation. A dew module owns no
    state the document owns: every parameter lives in the ValueTree, and DSP
    state is by definition not persisted. So getStateInformation belongs to the
    DESCRIPTOR, not to each module - two free functions written once and generic
    over every type, rather than a virtual pair that six classes would implement
    identically and one of them would eventually get wrong.

    Dry/wet is also absent, and that is the same argument. The host applies
    `mix` identically for every type, which is what keeps "a fully dry slot is
    bit-exact passthrough" true in exactly one place instead of six.
*/
class EffectModule
{
public:
    virtual ~EffectModule() = default;

    /** Message thread. May allocate. Called before any process(), and again on
        every sample-rate or block-size change. */
    virtual void prepare (double sampleRate, int maximumBlockSize) = 0;

    /** Audio thread. Clears every tail. Must not allocate. */
    virtual void reset() noexcept = 0;

    /** Audio thread, in place, FULLY WET. Must not allocate, lock, or throw. */
    virtual void process (ParamBlock, StereoView) noexcept = 0;

    /** Message thread. */
    virtual void releaseResources() {}
};

} // namespace dew
