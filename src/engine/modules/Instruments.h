#pragma once

#include "engine/InstrumentModule.h"
#include "engine/SamplePlayer.h"
#include "engine/SoundFontChannel.h"
#include "engine/SynthChannel.h"

namespace dew
{

/** The polyphonic synth: sixteen voices sharing an oscillator bank.

    Mono all the way down UNTIL a slot's LFO is asked to sweep it across the
    field, which is why this is the one instrument that does its own widening
    rather than inheriting MonoInstrumentModule's. It cannot inherit it and also
    have a side: that class's processAdd is final and hands a subclass exactly
    one scratch channel, both deliberately.

    What it does instead is REPRODUCE that widening, unchanged, and add the
    panned oscillators as a second, gated stage on top. So a project with no pan
    modulation is bit-identical to one rendered before there were LFOs by
    CONSTRUCTION - the same buffer, cleared the same way, summed in the same
    order, added twice - rather than by an argument about whether the channel
    buffer really was cleared first. See MonoInstrumentModule, which spells out
    why the two obvious shortcuts are wrong and why both fail quietly.
*/
class SynthInstrument final : public InstrumentModule
{
public:
    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;

    NoteMask soundingPitches() const noexcept override
    {
        return channel.soundingPitches();
    }

private:
    void processAdd (const InstrumentContext&, StereoView out) noexcept override;

    SynthChannel channel;

    /** Three channels: the mono sum, then the panned pair. Sized on the message
        thread in prepare(), never on the audio thread. */
    juce::AudioBuffer<float> scratch;
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

/** A soundfont's regions, played from note events.

    The one instrument that writes a genuinely different left and right: a
    region carries a pan, and a fifth of the samples in a real library are one
    half of a stereo pair. It therefore implements the stereo ABI directly
    rather than widening through MonoInstrumentModule.
*/
class SoundFontInstrument final : public InstrumentModule
{
public:
    void prepare (double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    void processAdd (const InstrumentContext&, StereoView out) noexcept override;

    NoteMask soundingPitches() const noexcept override
    {
        return channel.soundingPitches();
    }

private:
    SoundFontChannel channel;
};

} // namespace dew
