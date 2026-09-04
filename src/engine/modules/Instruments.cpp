#include "engine/modules/Instruments.h"

namespace dew
{

void SynthInstrument::prepareMono (double sampleRate, int)
{
    channel.prepare (sampleRate);
}

void SynthInstrument::reset() noexcept
{
    channel.reset();
}

void SynthInstrument::processAddMono (const InstrumentContext& ctx, float* out,
                                      int numSamples) noexcept
{
    if (ctx.osc == nullptr || ctx.amp == nullptr)
        return;

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

    // One read of each controller per block, and the live bank so a wavetable
    // position reaches notes already sounding.
    channel.renderAdd (out, numSamples, ctx.bendSemitones, ctx.modulation, ctx.osc);
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
                             ctx.stepsPerBar, ctx.transport.positionSamples,
                             *ctx.transport.tempoMap, ctx.transport.sampleRate);
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
