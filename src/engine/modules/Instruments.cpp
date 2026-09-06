#include "engine/modules/Instruments.h"

namespace dew
{

void SynthInstrument::prepare (double sampleRate, int maximumBlockSize)
{
    scratch.setSize (3, juce::jmax (1, maximumBlockSize));
    scratch.clear();
    channel.prepare (sampleRate);
}

void SynthInstrument::reset() noexcept
{
    channel.reset();
}

void SynthInstrument::processAdd (const InstrumentContext& ctx, StereoView out) noexcept
{
    if (ctx.osc == nullptr || ctx.amp == nullptr)
        return;

    auto* mono = scratch.getWritePointer (0);
    auto* panLeft = scratch.getWritePointer (1);
    auto* panRight = scratch.getWritePointer (2);

    const auto numSamples = juce::jmin (out.numSamples, scratch.getNumSamples());

    for (const auto& event : ctx.events)
    {
        switch (event.kind)
        {
            case NoteEvent::Kind::on:
                channel.noteOn (event.pitch, event.velocity, *ctx.osc, *ctx.amp,
                                event.durationSamples, event.sampleOffset);
                break;

            case NoteEvent::Kind::off: channel.noteOff (event.pitch); break;

            case NoteEvent::Kind::allOff: channel.allNotesOff(); break;
        }
    }

    // After the note-ons, because a note starting in this very block may be the
    // one that needs a side - and before the render, because the answer decides
    // whether there is a pair to clear.
    const auto panned = channel.hasPannedVoices();

    // Only numSamples, not the whole buffer: a render's last block is short, and
    // clearing less than it renders would sum the previous block's tail.
    juce::FloatVectorOperations::clear (mono, numSamples);

    if (panned)
    {
        juce::FloatVectorOperations::clear (panLeft, numSamples);
        juce::FloatVectorOperations::clear (panRight, numSamples);
    }

    // One read of each controller per block, and the live bank so a wavetable
    // position reaches notes already sounding.
    channel.renderAdd (mono, numSamples, ctx.bendSemitones, ctx.modulation, ctx.osc,
                       panned ? panLeft : nullptr, panned ? panRight : nullptr);

    // The two adds MonoInstrumentModule makes, written out here rather than
    // inherited - and identical to them, which is what keeps a project with no
    // pan modulation rendering the bits it always rendered.
    juce::FloatVectorOperations::add (out.left, mono, numSamples);
    juce::FloatVectorOperations::add (out.right, mono, numSamples);

    if (panned)
    {
        juce::FloatVectorOperations::add (out.left, panLeft, numSamples);
        juce::FloatVectorOperations::add (out.right, panRight, numSamples);
    }
}

void SampleInstrument::processAddMono (const InstrumentContext& ctx, float* out,
                                       int numSamples) noexcept
{
    // Audio clips live in the arrangement, so they sound in song mode only -
    // the same rule automation follows, and for the same reason: pattern mode
    // has no playlist position for a clip to cover.
    if (! ctx.transport.playing || ! ctx.transport.arrangement)
        return;

    if (ctx.sample == nullptr || ctx.audio == nullptr)
        return;

    SamplePlayer::renderAdd (out, numSamples, *ctx.sample, *ctx.audio, ctx.clips, ctx.channelIndex,
                             ctx.transport.positionSamples, *ctx.transport.tempoMap,
                             ctx.transport.sampleRate);
}

void SoundFontInstrument::prepare (double sampleRate, int)
{
    channel.prepare (sampleRate);
}

void SoundFontInstrument::reset() noexcept
{
    channel.reset();
}

void SoundFontInstrument::processAdd (const InstrumentContext& ctx, StereoView out) noexcept
{
    // A channel whose font is missing from this machine plays nothing, and says
    // so through the snapshot's warnings rather than here - the audio thread is
    // not where a person finds out that a file has moved.
    if (ctx.soundFont == nullptr || ctx.soundFontSettings == nullptr)
        return;

    const auto* preset = ctx.soundFont->presetFor (ctx.soundFontSettings->bank,
                                                   ctx.soundFontSettings->program);

    if (preset == nullptr)
        return;

    for (const auto& event : ctx.events)
    {
        switch (event.kind)
        {
            case NoteEvent::Kind::on:
                channel.noteOn (*ctx.soundFont, *preset, event.pitch, event.velocity,
                                *ctx.soundFontSettings, event.durationSamples, event.sampleOffset);
                break;

            case NoteEvent::Kind::off: channel.noteOff (event.pitch); break;

            case NoteEvent::Kind::allOff: channel.allNotesOff(); break;
        }
    }

    channel.renderAdd (out.left, out.right, out.numSamples);
}

} // namespace dew
