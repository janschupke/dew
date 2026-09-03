#pragma once

#include <vector>

#include "engine/EngineSnapshot.h"
#include "engine/TempoMap.h"
#include "engine/Transport.h"

namespace dew
{

/** A note to start, at a sample offset within the current block.

    There is no note-off event: the trigger carries how long the note lasts, and
    the voice releases itself. That removes all the bookkeeping around notes
    whose end falls outside the block, or past a loop point, and makes the
    sequencer stateless.
*/
struct NoteTrigger
{
    int sampleOffset = 0;
    int channelIndex = 0;
    int pitch = 60;
    float velocity = 1.0f;
    int durationSamples = 0;
};

/** Works out which notes start inside a block. Stateless: given the same
    snapshot and position it always produces the same triggers, which is what
    makes it testable without running an audio device.
*/
class Sequencer
{
public:
    /** Appends triggers for [positionSamples, positionSamples + numSamples).

        `out` is cleared first. It is a caller-owned vector so the audio thread
        can reuse its capacity instead of allocating per block.
    */
    static void collect (const EngineSnapshot& snapshot,
                         Transport::Mode mode,
                         juce::int64 positionSamples,
                         int numSamples,
                         const TempoMap& tempoMap,
                         double sampleRate,
                         int patternIndexForPatternMode,
                         std::vector<NoteTrigger>& out);

    /** How long the MATERIAL is, in steps, for the given mode: the pattern's
        length, or the arrangement's. Zero means nothing to play.

        Deliberately not called a loop length any more. It used to be one, because
        playback always wrapped the whole of whatever was playing - but a loop is
        now a window the user can choose, and the two facts had to stop sharing a
        name one line apart in AudioEngine::processBlock.
    */
    static int materialLengthSteps (const EngineSnapshot&, Transport::Mode, int patternIndex);
};

} // namespace dew
