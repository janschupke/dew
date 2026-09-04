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

    /** The playhead in SAMPLES, and how steps become time.

        Both, because an audio clip needs each for a different thing: its
        PLACEMENT is musical and follows the tempo map, but its PLAYBACK RATE is
        not - you do not time-stretch a recording because a tempo curve moved.
        Deriving one from the other would tie them together and do exactly that.
    */
    juce::int64 positionSamples = 0;
    const TempoMap* tempoMap = nullptr;

    bool playing = false;
    bool arrangement = false; ///< song mode, not pattern
};

/** One thing that happens to a note, at a sample within the coming block. */
struct NoteEvent
{
    enum class Kind
    {
        on,
        off,
        allOff
    };

    Kind kind = Kind::on;
    int sampleOffset = 0; ///< within the block - see SynthVoice::start
    int pitch = 0;
    float velocity = 1.0f;
    int durationSamples = 0; ///< the voice releases itself when this runs out
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

    /** The soundfont a soundfont channel plays, and how its knobs bend it.

        A raw pointer into the snapshot's shared_ptr, which is safe for the
        reason every other pointer in here is: the audio thread never releases
        a snapshot, so nothing it is handed can be freed underneath it.
    */
    const SoundFontData* soundFont = nullptr;
    const SoundFontSettings* soundFontSettings = nullptr;

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

    /** ADDS into a stereo pair.

        Adds rather than replaces, so two sources on one channel sum instead of
        one silently winning. That was SamplePlayer's rule; it is the ABI's now.
        A module that renders into `out.left` and copies it to `out.right` is
        breaking this contract even though the host happens to hand it a cleared
        pair today.

        Stereo rather than mono because a SoundFont carries linked stereo sample
        pairs and a per-region pan, and a fifth of a real library is stereo. The
        two mono instruments widen through MonoInstrumentModule below rather than
        each growing a second channel they have nothing to say about.
    */
    virtual void processAdd (const InstrumentContext&, StereoView out) noexcept = 0;
};

/** An instrument whose DSP is mono, widened to the ABI in one place.

    Both of the original instruments are mono all the way down: SynthVoice sums
    its oscillators to one value, and SamplePlayer folds a multi-channel source
    to mono on purpose. Neither has a second side to say anything about, and both
    are called DIRECTLY by the tests that pin the engine sample for sample - so
    the widening happens here, above them, and those tests keep proving what they
    proved.

    The obvious shortcuts are both wrong, and both fail quietly:

    - Calling the mono render twice, once per side, advances SynthVoice's phase
      and envelope twice a block, so the right channel would hold the NEXT n
      samples. Every existing test still passes, because they all drive
      SynthChannel below this layer.
    - Rendering into out.left and copying to out.right breaks the ADD contract
      above. It is harmless only while one module writes a freshly cleared pair,
      which is exactly why it would survive review.

    So: a private mono scratch, then two adds. Rendering into a cleared scratch
    produces the same bits as rendering into the cleared channel buffer, and
    0.0f + x == x for every finite x - there is no ScopedNoDenormals anywhere in
    src/, so denormals are not flushed either.
*/
class MonoInstrumentModule : public InstrumentModule
{
public:
    /** Final: SynthInstrument already overrode prepare(), and an override that
        skipped sizing the scratch would leave the audio thread writing into a
        zero-length buffer. Subclasses take prepareMono instead.
    */
    void prepare (double sampleRate, int maximumBlockSize) final
    {
        scratch.setSize (1, juce::jmax (1, maximumBlockSize));
        scratch.clear();
        prepareMono (sampleRate, maximumBlockSize);
    }

    void processAdd (const InstrumentContext& context, StereoView out) noexcept final
    {
        auto* mono = scratch.getWritePointer (0);
        const auto numSamples = juce::jmin (out.numSamples, scratch.getNumSamples());

        // Only numSamples, not the whole buffer: a render's last block is short,
        // and clearing less than it renders would sum the previous block's tail.
        juce::FloatVectorOperations::clear (mono, numSamples);

        processAddMono (context, mono, numSamples);

        juce::FloatVectorOperations::add (out.left, mono, numSamples);
        juce::FloatVectorOperations::add (out.right, mono, numSamples);
    }

protected:
    virtual void prepareMono (double sampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused (sampleRate, maximumBlockSize);
    }

    /** ADDS mono output, exactly as the ABI read before it was widened. */
    virtual void processAddMono (const InstrumentContext&, float* out, int numSamples) noexcept = 0;

private:
    /** Message thread only - sized in prepare(), never on the audio thread. */
    juce::AudioBuffer<float> scratch;
};

} // namespace dew
