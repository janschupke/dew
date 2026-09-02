#pragma once

#include "engine/InstrumentModule.h"
#include "engine/SamplePlayer.h"
#include "engine/SynthChannel.h"

namespace dew
{

/** The polyphonic synth: sixteen voices sharing an oscillator bank. */
class SynthInstrument final : public InstrumentModule
{
public:
    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void processAdd (const InstrumentContext&, float* out, int numSamples) noexcept override;

private:
    SynthChannel channel;
};

/** An audio channel's clips, read at a position.

    Holds no state at all - SamplePlayer works the read offset out from the
    transport each block, which is what makes seeking, looping and rewinding the
    same arithmetic. It is a module so that the render loop has one shape, not
    because it needed somewhere to keep something.
*/
class SampleInstrument final : public InstrumentModule
{
public:
    void prepare (double, int) override {}
    void reset() noexcept override {}
    void processAdd (const InstrumentContext&, float* out, int numSamples) noexcept override;
};

} // namespace dew
