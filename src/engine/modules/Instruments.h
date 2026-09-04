#pragma once

#include "engine/InstrumentModule.h"
#include "engine/SamplePlayer.h"
#include "engine/SynthChannel.h"

namespace dew
{

/** The polyphonic synth: sixteen voices sharing an oscillator bank.

    Mono all the way down - a voice sums its oscillators to one value - so it
    widens to the stereo ABI through MonoInstrumentModule rather than growing a
    second channel it has nothing to put in.
*/
class SynthInstrument final : public MonoInstrumentModule
{
public:
    void reset() noexcept override;

private:
    void prepareMono (double sampleRate, int maximumBlockSize) override;
    void processAddMono (const InstrumentContext&, float* out, int numSamples) noexcept override;

    SynthChannel channel;
};

/** An audio channel's clips, read at a position.

    Holds no state at all - SamplePlayer works the read offset out from the
    transport each block, which is what makes seeking, looping and rewinding the
    same arithmetic. It is a module so that the render loop has one shape, not
    because it needed somewhere to keep something.
*/
class SampleInstrument final : public MonoInstrumentModule
{
public:
    void reset() noexcept override {}

private:
    void processAddMono (const InstrumentContext&, float* out, int numSamples) noexcept override;
};

} // namespace dew
