#pragma once

#include <span>

#include "engine/EngineSnapshot.h"
#include "engine/Module.h"

namespace dew
{

/** Where the transport is, as much of it as an instrument needs. */
struct TransportView
{
    double positionSteps = 0.0;
    double samplesPerStep = 0.0;
    double sampleRate = kDefaultSampleRate;
    bool playing = false;
    bool arrangement = false;   ///< song mode, not pattern
};

/** One thing that happens to a note, at a sample within the coming block. */
struct NoteEvent
{
    enum class Kind { on, off, allOff };

    Kind kind = Kind::on;
    int sampleOffset = 0;       ///< within the block - see SynthVoice::start
    int pitch = 0;
    float velocity = 1.0f;
    int durationSamples = 0;    ///< the voice releases itself when this runs out
};

/** Everything an instrument needs for one block.

    Notes arrive as a span of events rather than as method calls, which is the
    shape a plugin's processBlock receives them in - and it is what lets a
    module be handed a whole block's worth of timing rather than being poked
    once per note by whoever happens to be iterating.

    The rest is structured rather than a flat parameter block, and that is an
    honest asymmetry with EffectModule. An oscillator bank is three slots of a
    dozen fields with per-slot mode discrimination; flattening it would mean
    rewriting SynthVoice::start and renderAdd, which is exactly the code four
    test files pin sample for sample. Doing both at once would leave a
    bit-exactness failure with two candidate causes. The declarative half is
    generic already - the synth's parameters are ParamSpec rows like any other -
    so a future AudioProcessor wrapper is mechanical either way.
*/
struct InstrumentContext
{
    TransportView transport;
    juce::Span<const NoteEvent> events {};

    float bendSemitones = 0.0f;
    float modulation = 0.0f;

    // A module reads its own and ignores the rest, the way an effect reads the
    // fields of the block its type declares.
    const OscBankSnapshot* osc = nullptr;
    const AmpSettings* amp = nullptr;
    const SampleSettings* sample = nullptr;
    const juce::AudioBuffer<float>* audio = nullptr;

    /** Every clip in the arrangement, in the snapshot's own order, plus which
        channel is asking. Deliberately not pre-filtered: Sequencer walks this
        same vector and its iteration order decides note-trigger order, which
        decides voice stealing, so a copy sorted differently would change what
        you hear.
    */
    juce::Span<const ClipSnapshot> clips {};
    int channelIndex = -1;
    int stepsPerBar = 16;
};

/** One instrument, as a module. */
class InstrumentModule
{
public:
    virtual ~InstrumentModule() = default;

    virtual void prepare (double sampleRate, int maximumBlockSize) = 0;
    virtual void reset() noexcept = 0;
    virtual void releaseResources() {}

    /** ADDS mono output.

        Adds rather than replaces, so two sources on one channel sum instead of
        one silently winning. That was SamplePlayer's rule; it is the ABI's now.
    */
    virtual void processAdd (const InstrumentContext&, float* out, int numSamples) noexcept = 0;
};

} // namespace dew
