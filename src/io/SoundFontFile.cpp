#include "io/SoundFontFile.h"

#include "io/SoundFontFormat.h"

#include <algorithm>
#include <cmath>

namespace dew
{

using namespace sf2;

namespace
{

/** The generators of one zone, as a half-open range over the generator list.

    A bag says where its zone STARTS; the zone ends where the next bag starts,
    and the last one ends at the end of the list. That indirection is the single
    place a malformed font is most likely to point somewhere it should not, so
    both ends are clamped rather than trusted.
*/
struct ZoneRange
{
    size_t first = 0, last = 0;
};

ZoneRange zoneRangeFor (const std::vector<Bag>& bags, size_t bagIndex, size_t bagEnd,
                        size_t generatorCount)
{
    const auto begin = (size_t) juce::jmax (0, bags[bagIndex].generatorIndex);

    const auto end = bagIndex + 1 < bagEnd
                         ? (size_t) juce::jmax (0, bags[bagIndex + 1].generatorIndex)
                         : generatorCount;

    return { juce::jmin (begin, generatorCount),
             juce::jmin (juce::jmax (begin, end), generatorCount) };
}

} // namespace

// ==============================================================================

SoundFontFile::Result SoundFontFile::parse (const void* data, size_t sizeInBytes,
                                            const juce::String& name)
{
    Result result;
    result.font.name = name;

    const Span file { (const juce::uint8*) data, sizeInBytes };
    SoundFontReader reader { file, result.warnings };

    if (! reader.findChunks())
        return result;

    if (reader.chunks.phdr.data == nullptr || reader.chunks.shdr.data == nullptr
        || reader.chunks.igen.data == nullptr)
    {
        reader.warn ("This font has no preset data; there is nothing to play.");
        return result;
    }

    // --- the sample pool ------------------------------------------------------
    const auto sampleCount = reader.chunks.smpl.size / 2;
    result.font.pcm.resize (sampleCount);

    for (size_t i = 0; i < sampleCount; ++i)
        result.font.pcm[i] = (juce::int16) readU16 (reader.chunks.smpl.data + i * 2);

    const auto presets = reader.readPresetHeaders();
    const auto instruments = reader.readInstrumentHeaders();
    const auto presetBags = reader.readBags (reader.chunks.pbag);
    const auto instrumentBags = reader.readBags (reader.chunks.ibag);
    const auto presetGens = reader.readGenerators (reader.chunks.pgen);
    const auto instrumentGens = reader.readGenerators (reader.chunks.igen);
    const auto samples = reader.readSampleHeaders();

    // Every list ends with a terminal record naming nothing - "EOP", "EOI",
    // "EOS" - which exists so the one before it has somewhere to say it ends.
    if (presets.size() < 2 || instruments.size() < 2 || samples.size() < 2)
    {
        reader.warn ("This font's preset, instrument or sample list is empty.");
        return result;
    }

    std::array<bool, kNumGenerators> cutSeen {};

    // --- one sample zone, resolved into a region ------------------------------
    const auto makeRegion = [&] (const GeneratorSet& gens, SoundFontRegion& region) -> bool
    {
        const auto sampleIndex = (size_t) juce::jmax (0, gens[kGenSampleID]);

        if (sampleIndex + 1 >= samples.size())
        {
            reader.warn ("A zone names a sample this font does not contain.");
            return false;
        }

        const auto& header = samples[sampleIndex];

        // ROM samples live in hardware this program does not have.
        if ((header.type & 0x8000) != 0)
        {
            reader.warn ("A zone plays a ROM sample, which cannot be read from the file.");
            return false;
        }

        const auto offset = [&gens] (int fine, int coarse)
        { return (juce::int64) gens[fine] + (juce::int64) gens[coarse] * 32768; };

        const auto poolEnd = (juce::int64) result.font.pcm.size();

        const auto clampToPool = [poolEnd] (juce::int64 v)
        { return (juce::uint32) juce::jlimit ((juce::int64) 0, poolEnd, v); };

        const auto start = clampToPool (
            (juce::int64) header.start + offset (kGenStartAddrsOffset, kGenStartAddrsCoarseOffset));
        const auto end = clampToPool ((juce::int64) header.end
                                      + offset (kGenEndAddrsOffset, kGenEndAddrsCoarseOffset));

        if (end <= start)
        {
            reader.warn ("A zone's sample is empty once its offsets are applied.");
            return false;
        }

        region.start = start;
        region.end = end;

        auto loopStart = clampToPool (
            (juce::int64) header.loopStart
            + offset (kGenStartloopAddrsOffset, kGenStartloopAddrsCoarseOffset));
        auto loopEnd = clampToPool (
            (juce::int64) header.loopEnd
            + offset (kGenEndloopAddrsOffset, kGenEndloopAddrsCoarseOffset));

        const auto mode = gens[kGenSampleModes] & 3;
        region.loop = mode == 1   ? SoundFontLoop::continuous
                      : mode == 3 ? SoundFontLoop::untilRelease
                                  : SoundFontLoop::none;

        // A loop outside the sample it belongs to is the most common corruption
        // in the wild, and the one that would read out of bounds every block.
        if (region.loop != SoundFontLoop::none
            && (loopEnd <= loopStart || loopStart < start || loopEnd > end))
        {
            reader.warn ("A zone's loop points lie outside its sample; it will not loop.");
            region.loop = SoundFontLoop::none;
            loopStart = start;
            loopEnd = end;
        }

        region.loopStart = loopStart;
        region.loopEnd = loopEnd;

        region.lowKey = juce::jlimit (0, 127, gens[kGenKeyRange] & 0xff);
        region.highKey = juce::jlimit (0, 127, (gens[kGenKeyRange] >> 8) & 0xff);
        region.lowVelocity = juce::jlimit (0, 127, gens[kGenVelRange] & 0xff);
        region.highVelocity = juce::jlimit (0, 127, (gens[kGenVelRange] >> 8) & 0xff);

        const auto root = gens[kGenOverridingRootKey];
        region.rootKey = root >= 0 && root <= 127 ? root : header.originalPitch;

        region.tuneCents = (float) (gens[kGenCoarseTune] * 100 + gens[kGenFineTune]
                                    + header.correctionCents);
        region.scaleTuning = (float) juce::jlimit (0, 1200, gens[kGenScaleTuning]);
        region.sampleRate = header.sampleRate > 0 ? (double) header.sampleRate : 44100.0;

        region.gain = centibelsToGain (gens[kGenInitialAttenuation]);

        // A stereo pair whose author left the pan generator alone is hard-panned
        // by which half of the pair it is. Without this a piano's two sides sum
        // to the centre and the file's stereo image is simply lost - and 20% of
        // a real library is stereo.
        constexpr int kSampleTypeRight = 2;
        constexpr int kSampleTypeLeft = 4;

        if (gens.has (kGenPan))
            region.pan = juce::jlimit (-1.0f, 1.0f, (float) gens[kGenPan] / 500.0f);
        else if ((header.type & kSampleTypeLeft) != 0)
            region.pan = -1.0f;
        else if ((header.type & kSampleTypeRight) != 0)
            region.pan = 1.0f;

        const auto envelope = [&gens] (int delay, int attack, int hold, int decay, int sustain,
                                       int release, bool sustainIsPermille)
        {
            SoundFontEnvelope e;
            e.delaySeconds = timecentsToSeconds (gens[delay]);
            e.attackSeconds = timecentsToSeconds (gens[attack]);
            e.holdSeconds = timecentsToSeconds (gens[hold]);
            e.decaySeconds = timecentsToSeconds (gens[decay]);
            e.releaseSeconds = timecentsToSeconds (gens[release]);

            // The two sustains are in DIFFERENT units, which is a genuine trap
            // in the format: the volume envelope's is centibels of attenuation,
            // the modulation envelope's is tenths of a percent of decrease.
            e.sustainLevel = sustainIsPermille
                                 ? juce::jlimit (0.0f, 1.0f, 1.0f - (float) gens[sustain] / 1000.0f)
                                 : centibelsToGain (gens[sustain]);
            return e;
        };

        region.volumeEnvelope = envelope (kGenDelayVolEnv, kGenAttackVolEnv, kGenHoldVolEnv,
                                          kGenDecayVolEnv, kGenSustainVolEnv, kGenReleaseVolEnv,
                                          /*sustainIsPermille*/ false);
        region.modEnvelope = envelope (kGenDelayModEnv, kGenAttackModEnv, kGenHoldModEnv,
                                       kGenDecayModEnv, kGenSustainModEnv, kGenReleaseModEnv,
                                       /*sustainIsPermille*/ true);

        region.modEnvToFilterCents = (float) juce::jlimit (-12000, 12000,
                                                           gens[kGenModEnvToFilterFc]);

        region.filterCutoffHz = absoluteCentsToHz (
            juce::jlimit (1500, 13500, gens[kGenInitialFilterFc]));
        region.filterQ = (float) juce::jlimit (0, 960, gens[kGenInitialFilterQ]) / 10.0f;

        region.exclusiveClass = juce::jlimit (0, 127, gens[kGenExclusiveClass]);

        return true;
    };

    // --- walk preset zones, then the instrument zones they name ---------------
    for (size_t p = 0; p + 1 < presets.size(); ++p)
    {
        SoundFontPreset preset;
        preset.name = presets[p].name;
        preset.program = juce::jlimit (0, 127, presets[p].program);
        preset.bank = juce::jlimit (0, 128, presets[p].bank);

        const auto firstBag = (size_t) juce::jmax (0, presets[p].bagIndex);
        const auto lastBag = (size_t) juce::jmax (0, presets[p + 1].bagIndex);

        GeneratorSet globalPreset;
        bool haveGlobalPreset = false;

        for (size_t b = firstBag; b < lastBag && b < presetBags.size(); ++b)
        {
            const auto range = zoneRangeFor (presetBags, b, presetBags.size(), presetGens.size());

            GeneratorSet zone;
            int instrumentIndex = -1;

            for (auto g = range.first; g < range.last; ++g)
            {
                const auto& gen = presetGens[g];

                if (gen.oper == kGenInstrument)
                    instrumentIndex = gen.asUnsigned();
                else if (gen.oper == kGenKeyRange || gen.oper == kGenVelRange)
                    zone.set (gen.oper, gen.rangeLow() | (gen.rangeHigh() << 8));
                else
                    zone.set (gen.oper, gen.asSigned());
            }

            // A zone that names no instrument is the preset's global zone, and
            // is only meaningful as the first one.
            if (instrumentIndex < 0)
            {
                if (! haveGlobalPreset)
                {
                    globalPreset = zone;
                    haveGlobalPreset = true;
                }

                continue;
            }

            if ((size_t) instrumentIndex + 1 >= instruments.size())
            {
                reader.warn ("A preset names an instrument this font does not contain.");
                continue;
            }

            const auto firstInstrumentBag = (size_t) juce::jmax (
                0, instruments[(size_t) instrumentIndex].bagIndex);
            const auto lastInstrumentBag = (size_t) juce::jmax (
                0, instruments[(size_t) instrumentIndex + 1].bagIndex);

            GeneratorSet globalInstrument = defaultGenerators();
            bool haveGlobalInstrument = false;

            for (size_t ib = firstInstrumentBag;
                 ib < lastInstrumentBag && ib < instrumentBags.size(); ++ib)
            {
                const auto instrumentRange = zoneRangeFor (
                    instrumentBags, ib, instrumentBags.size(), instrumentGens.size());

                auto gens = globalInstrument;
                bool namesSample = false;

                for (auto g = instrumentRange.first; g < instrumentRange.last; ++g)
                {
                    const auto& gen = instrumentGens[g];

                    for (const auto& cut : cutGenerators)
                        if (gen.oper == cut.oper)
                            cutSeen[(size_t) cut.oper] = true;

                    if (gen.oper == kGenSampleID)
                    {
                        gens.set (kGenSampleID, gen.asUnsigned());
                        namesSample = true;
                    }
                    else if (gen.oper == kGenKeyRange || gen.oper == kGenVelRange)
                    {
                        gens.set (gen.oper, gen.rangeLow() | (gen.rangeHigh() << 8));
                    }
                    else
                    {
                        gens.set (gen.oper, gen.asSigned());
                    }
                }

                if (! namesSample)
                {
                    if (! haveGlobalInstrument)
                    {
                        globalInstrument = gens;
                        haveGlobalInstrument = true;
                    }

                    continue;
                }

                // The preset's generators are OFFSETS onto the instrument's
                // absolute values - except the two ranges, which INTERSECT.
                // Adding a key range would be meaningless; a preset narrows what
                // its instrument answers to rather than moving it.
                for (int oper = 0; oper < kNumGenerators; ++oper)
                {
                    const auto fromZone = zone.has (oper);
                    const auto fromGlobal = globalPreset.has (oper);

                    if (! fromZone && ! fromGlobal)
                        continue;

                    const auto amount = fromZone ? zone[oper] : globalPreset[oper];

                    if (oper == kGenKeyRange || oper == kGenVelRange)
                    {
                        const auto low = juce::jmax (gens[oper] & 0xff, amount & 0xff);
                        const auto high = juce::jmin ((gens[oper] >> 8) & 0xff,
                                                      (amount >> 8) & 0xff);
                        gens.set (oper, low | (high << 8));
                    }
                    else if (isOffsetableAtPresetLevel (oper))
                    {
                        gens.add (oper, amount);
                    }
                }

                SoundFontRegion region;

                if (! makeRegion (gens, region))
                    continue;

                if (region.lowKey > region.highKey || region.lowVelocity > region.highVelocity)
                    continue; // the intersection above emptied it

                preset.regions.push_back (region);
            }
        }

        if (! preset.regions.empty())
            result.font.presets.push_back (std::move (preset));
    }

    for (const auto& cut : cutGenerators)
        if (cutSeen[(size_t) cut.oper])
            reader.warn (juce::String ("This font uses ") + cut.name + ", which is not applied.");

    if (result.font.presets.empty())
        reader.warn ("No preset in this font resolved to anything playable.");

    return result;
}

SoundFontFile::Result SoundFontFile::read (const juce::File& file)
{
    Result result;
    result.font.name = file.getFileNameWithoutExtension();

    juce::MemoryBlock bytes;

    if (! file.existsAsFile() || ! file.loadFileAsData (bytes))
    {
        result.warnings.add ("Could not read " + file.getFileName() + ".");
        return result;
    }

    return parse (bytes.getData(), bytes.getSize(), file.getFileNameWithoutExtension());
}

bool SoundFontFile::isSoundFont (const juce::File& file)
{
    // Case-insensitively, because a real library holds files spelled .SF2 and an
    // exact compare simply hides them.
    return file.getFileExtension().equalsIgnoreCase (".sf2");
}

} // namespace dew
