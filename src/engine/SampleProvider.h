#pragma once

#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "model/Constants.h"

namespace dew
{

/** Where a snapshot gets the audio a channel refers to.

    buildSnapshot needs to turn a stored path into samples, and the thing that
    does that - SamplePool - reads files, so it belongs with the rest of the
    code that touches the OS. Without this interface the engine would depend on
    the io layer for one call, and "the engine opens no files and no devices"
    would stop being true.

    That property is worth an abstract base: it is what lets the whole test
    suite drive the engine with no device and no message loop, and it is what a
    plugin wrapper would need later, since a plugin must not go looking at the
    filesystem on its host's behalf.

    Message thread only. Implementations may read from disk.
*/
struct SampleProvider
{
    virtual ~SampleProvider() = default;

    /** The audio a SAMPLE node's stored path refers to, or null if it cannot be
        read. `sourceSampleRate` is the file's own rate, left untouched when the
        result is null.

        Paths are stored relative to the project, so resolving one is the
        provider's job: a ValueTree does not know where its own file is.
    */
    virtual std::shared_ptr<const juce::AudioBuffer<float>>
        audioFor (const juce::String& storedPath, double& sourceSampleRate) = 0;
};

} // namespace dew
