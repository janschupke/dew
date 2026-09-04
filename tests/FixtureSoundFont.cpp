#include "FixtureSoundFont.h"

namespace dew::testing
{

namespace
{

void appendU16 (juce::MemoryOutputStream& out, int value)
{
    out.writeShort ((short) (juce::uint16) (value & 0xffff));
}

void appendU32 (juce::MemoryOutputStream& out, juce::uint32 value)
{
    out.writeInt ((int) value);
}

/** A fixed-width, NUL-padded name field. */
void appendName (juce::MemoryOutputStream& out, const juce::String& name, int width)
{
    const auto utf8 = name.toRawUTF8();
    const auto length = juce::jmin (width - 1, (int) strlen (utf8));

    out.write (utf8, (size_t) length);

    for (int i = length; i < width; ++i)
        out.writeByte (0);
}

/** A chunk, written as its four-character id, its length, its body and the pad
    byte an odd length carries. */
void appendChunk (juce::MemoryOutputStream& out, const char* id, const juce::MemoryBlock& body)
{
    out.write (id, 4);
    appendU32 (out, (juce::uint32) body.getSize());
    out.write (body.getData(), body.getSize());

    if ((body.getSize() & 1) != 0)
        out.writeByte (0);
}

juce::MemoryBlock listOf (const char* type, const juce::MemoryBlock& body)
{
    juce::MemoryBlock block;
    juce::MemoryOutputStream out (block, false);
    out.write (type, 4);
    out.write (body.getData(), body.getSize());
    out.flush();
    return block;
}

} // namespace

std::vector<juce::int16> SoundFontBuilder::rampSamples (int length)
{
    std::vector<juce::int16> data ((size_t) juce::jmax (0, length));

    // Full scale across its whole length, so the drop at a loop point is
    // something a test can actually measure. A gentle ramp reads as silence
    // next to the thresholds anything else in the suite uses.
    const auto span = juce::jmax ((size_t) 1, data.size() - 1);

    for (size_t i = 0; i < data.size(); ++i)
        data[i] = (juce::int16) ((i * 32000) / span);

    return data;
}

juce::MemoryBlock SoundFontBuilder::build() const
{
    // --- sdta: every sample's data, one after another -------------------------
    // The format puts forty-six frames of silence between samples so a read
    // that runs on cannot wander into the next one; the fixture keeps that.
    constexpr int kGuardFrames = 46;

    juce::MemoryBlock pcm;
    juce::MemoryOutputStream pcmOut (pcm, false);

    std::vector<juce::uint32> starts, ends;

    for (const auto& sample : samples)
    {
        starts.push_back ((juce::uint32) (pcmOut.getPosition() / 2));

        for (const auto value : sample.data)
            pcmOut.writeShort ((short) value);

        ends.push_back ((juce::uint32) (pcmOut.getPosition() / 2));

        for (int i = 0; i < kGuardFrames; ++i)
            pcmOut.writeShort (0);
    }

    pcmOut.flush();

    // --- pdta: the four record lists, each with its terminal entry ------------
    juce::MemoryBlock phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    juce::MemoryOutputStream phdrOut (phdr, false), pbagOut (pbag, false), pgenOut (pgen, false);
    juce::MemoryOutputStream instOut (inst, false), ibagOut (ibag, false), igenOut (igen, false);
    juce::MemoryOutputStream shdrOut (shdr, false);

    constexpr int kGenInstrument = 41;
    constexpr int kGenSampleID = 53;

    for (const auto& preset : presets)
    {
        appendName (phdrOut, preset.name, 20);
        appendU16 (phdrOut, preset.program);
        appendU16 (phdrOut, preset.bank);
        appendU16 (phdrOut, (int) (pbagOut.getPosition() / 4));
        appendU32 (phdrOut, 0); // library
        appendU32 (phdrOut, 0); // genre
        appendU32 (phdrOut, 0); // morphology

        for (const auto& zone : preset.zones)
        {
            appendU16 (pbagOut, (int) (pgenOut.getPosition() / 4));
            appendU16 (pbagOut, 0);

            for (const auto& gen : zone.generators)
            {
                appendU16 (pgenOut, gen.oper);
                appendU16 (pgenOut, gen.amount);
            }

            // The terminal generator, which is what makes this a zone that
            // names an instrument rather than the preset's global one.
            if (zone.instrumentIndex >= 0)
            {
                appendU16 (pgenOut, kGenInstrument);
                appendU16 (pgenOut, zone.instrumentIndex);
            }
        }
    }

    // "EOP": a terminal record whose bag index says where the last real preset
    // ends. Without it the reader has no end for the preset before it.
    appendName (phdrOut, "EOP", 20);
    appendU16 (phdrOut, 0);
    appendU16 (phdrOut, 0);
    appendU16 (phdrOut, (int) (pbagOut.getPosition() / 4));
    appendU32 (phdrOut, 0);
    appendU32 (phdrOut, 0);
    appendU32 (phdrOut, 0);

    appendU16 (pbagOut, (int) (pgenOut.getPosition() / 4));
    appendU16 (pbagOut, 0);

    appendU16 (pgenOut, 0);
    appendU16 (pgenOut, 0);

    for (const auto& instrument : instruments)
    {
        appendName (instOut, instrument.name, 20);
        appendU16 (instOut, (int) (ibagOut.getPosition() / 4));

        for (const auto& zone : instrument.zones)
        {
            appendU16 (ibagOut, (int) (igenOut.getPosition() / 4));
            appendU16 (ibagOut, 0);

            for (const auto& gen : zone.generators)
            {
                appendU16 (igenOut, gen.oper);
                appendU16 (igenOut, gen.amount);
            }

            if (zone.sampleIndex >= 0)
            {
                appendU16 (igenOut, kGenSampleID);
                appendU16 (igenOut, zone.sampleIndex);
            }
        }
    }

    appendName (instOut, "EOI", 20);
    appendU16 (instOut, (int) (ibagOut.getPosition() / 4));

    appendU16 (ibagOut, (int) (igenOut.getPosition() / 4));
    appendU16 (ibagOut, 0);

    appendU16 (igenOut, 0);
    appendU16 (igenOut, 0);

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& sample = samples[i];

        appendName (shdrOut, sample.name, 20);
        appendU32 (shdrOut, starts[i]);
        appendU32 (shdrOut, ends[i]);
        appendU32 (shdrOut, starts[i] + sample.loopStart);
        appendU32 (shdrOut,
                   starts[i]
                       + (sample.loopEnd > 0 ? sample.loopEnd : (juce::uint32) sample.data.size()));
        appendU32 (shdrOut, sample.sampleRate);
        shdrOut.writeByte ((char) (juce::uint8) sample.rootKey);
        shdrOut.writeByte ((char) (juce::int8) sample.correctionCents);
        appendU16 (shdrOut, 0); // sampleLink, deliberately unused - see the reader
        appendU16 (shdrOut, sample.type);
    }

    appendName (shdrOut, "EOS", 20);

    for (int i = 0; i < 5; ++i)
        appendU32 (shdrOut, 0);

    shdrOut.writeByte (0);
    shdrOut.writeByte (0);
    appendU16 (shdrOut, 0);
    appendU16 (shdrOut, 0);

    // A modulator list is a single terminal record, which is what a font that
    // uses none looks like.
    juce::MemoryOutputStream pmodOut (pmod, false), imodOut (imod, false);

    for (auto* stream : { &pmodOut, &imodOut })
        for (int i = 0; i < 5; ++i)
            appendU16 (*stream, 0);

    for (auto* stream : { &phdrOut, &pbagOut, &pgenOut, &instOut, &ibagOut, &igenOut, &shdrOut,
                          &pmodOut, &imodOut })
        stream->flush();

    // --- assembly -------------------------------------------------------------
    const auto wanted = [this] (const char* id) { return ! omit.contains (id); };

    juce::MemoryBlock info;
    juce::MemoryOutputStream infoOut (info, false);
    {
        juce::MemoryBlock ifil;
        juce::MemoryOutputStream ifilOut (ifil, false);
        appendU16 (ifilOut, 2);
        appendU16 (ifilOut, 1);
        ifilOut.flush();
        appendChunk (infoOut, "ifil", ifil);

        juce::MemoryBlock name;
        juce::MemoryOutputStream nameOut (name, false);
        appendName (nameOut, "Fixture", 20);
        nameOut.flush();
        appendChunk (infoOut, "INAM", name);
    }
    infoOut.flush();

    juce::MemoryBlock sdta;
    juce::MemoryOutputStream sdtaOut (sdta, false);

    if (wanted ("smpl"))
        appendChunk (sdtaOut, "smpl", pcm);

    sdtaOut.flush();

    juce::MemoryBlock pdta;
    juce::MemoryOutputStream pdtaOut (pdta, false);

    const std::pair<const char*, const juce::MemoryBlock*> pdtaChunks[] {
        { "phdr", &phdr }, { "pbag", &pbag }, { "pmod", &pmod },
        { "pgen", &pgen }, { "inst", &inst }, { "ibag", &ibag },
        { "imod", &imod }, { "igen", &igen }, { "shdr", &shdr }
    };

    for (const auto& [id, body] : pdtaChunks)
        if (wanted (id))
            appendChunk (pdtaOut, id, *body);

    pdtaOut.flush();

    juce::MemoryBlock body;
    juce::MemoryOutputStream bodyOut (body, false);
    bodyOut.write ("sfbk", 4);
    appendChunk (bodyOut, "LIST", listOf ("INFO", info));

    if (wanted ("sdta"))
        appendChunk (bodyOut, "LIST", listOf ("sdta", sdta));

    if (wanted ("pdta"))
        appendChunk (bodyOut, "LIST", listOf ("pdta", pdta));

    bodyOut.flush();

    juce::MemoryBlock file;
    juce::MemoryOutputStream fileOut (file, false);
    fileOut.write ("RIFF", 4);
    appendU32 (fileOut, (juce::uint32) body.getSize());
    fileOut.write (body.getData(), body.getSize());
    fileOut.flush();

    return file;
}

SoundFontBuilder SoundFontBuilder::minimal()
{
    constexpr int kGenKeyRange = 43;
    constexpr int kGenSampleModes = 54;
    constexpr int kGenOverridingRootKey = 58;

    SoundFontBuilder builder;

    Sample sample;
    sample.name = "Ramp";
    sample.data = rampSamples (200);
    sample.rootKey = 60;
    sample.loopStart = 50;
    sample.loopEnd = 150;
    builder.samples.push_back (sample);

    Instrument instrument;
    instrument.name = "Ramp";
    instrument.zones.push_back ({ { { kGenKeyRange, 0 | (127 << 8) },
                                    { kGenSampleModes, 1 },
                                    { kGenOverridingRootKey, 60 } },
                                  0 });
    builder.instruments.push_back (instrument);

    Preset preset;
    preset.name = "Ramp";
    preset.zones.push_back ({ {}, 0 });
    builder.presets.push_back (preset);

    return builder;
}

SoundFontBuilder SoundFontBuilder::stereo()
{
    constexpr int kGenKeyRange = 43;
    constexpr int kGenOverridingRootKey = 58;
    constexpr int kSampleTypeRight = 2;
    constexpr int kSampleTypeLeft = 4;

    SoundFontBuilder builder;

    for (const auto& [name, type] :
         { std::pair { "Left", kSampleTypeLeft }, std::pair { "Right", kSampleTypeRight } })
    {
        Sample sample;
        sample.name = name;
        sample.data = rampSamples (200);
        sample.rootKey = 60;
        sample.type = type;
        builder.samples.push_back (sample);
    }

    Instrument instrument;
    instrument.name = "Pair";

    for (int i = 0; i < 2; ++i)
        instrument.zones.push_back (
            { { { kGenKeyRange, 0 | (127 << 8) }, { kGenOverridingRootKey, 60 } }, i });

    builder.instruments.push_back (instrument);

    Preset preset;
    preset.name = "Pair";
    preset.zones.push_back ({ {}, 0 });
    builder.presets.push_back (preset);

    return builder;
}

} // namespace dew::testing
