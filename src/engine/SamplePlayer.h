#pragma once

#include "engine/EngineSnapshot.h"
#include "engine/Transport.h"

namespace dew
{

/** Renders an audio channel's clips at a position.

    Position-driven rather than event-triggered, and that is the whole design.
    The Sequencer earns its statelessness by putting a note's LENGTH in the
    trigger so a voice can release itself; an audio clip has no equivalent, and
    an engine that started a sample on a "clip begins" event would then owe
    itself bookkeeping for every way a position can move that is not forwards:
    seeking into the middle of a clip, a loop wrapping back over its start,
    rewinding while it plays. Working the read offset out from the position
    each block makes all four of those the same arithmetic, and makes the whole
    thing a pure function that a test can check without a device.

    Everything is static for the same reason Sequencer is: given a snapshot and
    a position it always produces the same samples.
*/
class SamplePlayer
{
public:
    /** Adds whatever `channelIndex`'s audio clips are sounding over
        [positionSteps, positionSteps + numSamples) into `mono`.

        Adds rather than replaces, so two clips of the same channel that overlap
        sum instead of one silently winning. Never allocates, never locks.
    */
    static void renderAdd (float* mono, int numSamples,
                           const EngineSnapshot& snapshot,
                           int channelIndex,
                           double positionSteps,
                           double samplesPerStep,
                           double engineSampleRate) noexcept;

    /** The read position, in source frames from the region start, that output
        sample `outputOffset` of a clip reads from.

        Exposed so a test can assert where a seek lands without rendering.
    */
    static double readOffsetFor (const SampleSettings&, double elapsedOutputSamples,
                                 double engineSampleRate) noexcept;
};

} // namespace dew
