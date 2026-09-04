#pragma once

#include <array>
#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

#include "i18n/Strings.h"

namespace dew::sf2
{

/** The SoundFont 2 file format, as this reader needs to see it.

    INTERNAL. io/SoundFontFile.h is the public surface and declares three
    functions; nothing outside SoundFontFile.cpp and SoundFontFormat.cpp
    includes this.

    Everything here is about the FILE: the generator numbers the specification
    assigns, the bounds-checked little-endian reads, the fixed-width records,
    and the unit conversions that turn timecents and centibels into the seconds
    and gains the engine wants. What a font MEANS - which zone answers a note,
    what a region ends up being - is SoundFontFile.cpp, and it is a different
    kind of reasoning entirely.

    The conversions happen once at load rather than per note, which is why they
    are here rather than in the engine.
*/

// --- the format's generators, as the specification numbers them ---------------
// Only the ones this reader acts on. A generator it does not name is read,
// counted and ignored, which is what lets the warning list say what was skipped
// instead of the file silently sounding wrong.
constexpr int kGenStartAddrsOffset = 0;
constexpr int kGenEndAddrsOffset = 1;
constexpr int kGenStartloopAddrsOffset = 2;
constexpr int kGenEndloopAddrsOffset = 3;
constexpr int kGenStartAddrsCoarseOffset = 4;
constexpr int kGenInitialFilterFc = 8;
constexpr int kGenInitialFilterQ = 9;
constexpr int kGenEndAddrsCoarseOffset = 12;
constexpr int kGenPan = 17;
constexpr int kGenModEnvToFilterFc = 11;
constexpr int kGenDelayModEnv = 25;
constexpr int kGenAttackModEnv = 26;
constexpr int kGenHoldModEnv = 27;
constexpr int kGenDecayModEnv = 28;
constexpr int kGenSustainModEnv = 29;
constexpr int kGenReleaseModEnv = 30;
constexpr int kGenDelayVolEnv = 33;
constexpr int kGenAttackVolEnv = 34;
constexpr int kGenHoldVolEnv = 35;
constexpr int kGenDecayVolEnv = 36;
constexpr int kGenSustainVolEnv = 37;
constexpr int kGenReleaseVolEnv = 38;
constexpr int kGenInstrument = 41;
constexpr int kGenKeyRange = 43;
constexpr int kGenVelRange = 44;
constexpr int kGenStartloopAddrsCoarseOffset = 45;
constexpr int kGenKeynum = 46;
constexpr int kGenVelocity = 47;
constexpr int kGenInitialAttenuation = 48;
constexpr int kGenEndloopAddrsCoarseOffset = 50;
constexpr int kGenCoarseTune = 51;
constexpr int kGenFineTune = 52;
constexpr int kGenSampleID = 53;
constexpr int kGenSampleModes = 54;
constexpr int kGenScaleTuning = 56;
constexpr int kGenExclusiveClass = 57;
constexpr int kGenOverridingRootKey = 58;

constexpr int kNumGenerators = 60;

// The generators this reader deliberately does not act on, and what to call
// them when one turns up. Named rather than counted, so a font that needs
// something we cut says which thing.
struct CutGenerator
{
    int oper;
    const char* name;
};

constexpr CutGenerator cutGenerators[] { { 5, "modLfoToPitch" },       { 6, "vibLfoToPitch" },
                                         { 7, "modEnvToPitch" },       { 10, "modLfoToFilterFc" },
                                         { 13, "modLfoToVolume" },     { 15, "chorusEffectsSend" },
                                         { 16, "reverbEffectsSend" },  { 21, "delayModLFO" },
                                         { 22, "freqModLFO" },         { 23, "delayVibLFO" },
                                         { 24, "freqVibLFO" },         { 31, "keynumToModEnvHold" },
                                         { 32, "keynumToModEnvDecay" } };

// --- little-endian reads, every one of them bounds-checked -------------------

struct Span
{
    const juce::uint8* data = nullptr;
    size_t size = 0;

    bool has (size_t offset, size_t bytes) const noexcept
    {
        return data != nullptr && offset + bytes >= offset && offset + bytes <= size;
    }
};

inline juce::uint16 readU16 (const juce::uint8* p) noexcept
{
    return (juce::uint16) ((juce::uint16) p[0] | (juce::uint16) ((juce::uint16) p[1] << 8));
}

inline juce::uint32 readU32 (const juce::uint8* p) noexcept
{
    return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16)
           | ((juce::uint32) p[3] << 24);
}

/** A fixed-width name field. The format pads with NULs and does not promise
    one, so the length is bounded by the field rather than by a terminator. */
inline juce::String readName (const juce::uint8* p, size_t maximumLength)
{
    size_t length = 0;

    while (length < maximumLength && p[length] != 0)
        ++length;

    return juce::String::fromUTF8 ((const char*) p, (int) length).trim();
}

// --- unit conversions, done once at load rather than per note ----------------

inline float timecentsToSeconds (int timecents) noexcept
{
    // -12000 is the format's "immediately", and every default is exactly that.
    // Below it the exponential is meaninglessly small and costs a pow.
    if (timecents <= -12000)
        return 0.0f;

    return std::pow (2.0f, (float) timecents / 1200.0f);
}

inline float centibelsToGain (int centibels) noexcept
{
    if (centibels <= 0)
        return 1.0f;

    if (centibels >= 1440)
        return 0.0f;

    return std::pow (10.0f, -(float) centibels / 200.0f);
}

inline float absoluteCentsToHz (int cents) noexcept
{
    return 8.176f * std::pow (2.0f, (float) cents / 1200.0f);
}

// --- one zone's generators, resolved -----------------------------------------

/** Every generator's value for one zone, as ints.

    An array rather than a map because the set is closed and small, and because
    resolution is three passes over it: the specification's defaults, then the
    instrument's own absolute values, then the preset's offsets ADDED on top.
    That order is the format's, and getting it backwards is the classic way to
    make every preset in a font sound like its first one.
*/
struct GeneratorSet
{
    std::array<int, kNumGenerators> value {};
    std::array<bool, kNumGenerators> present {};

    void set (int oper, int amount) noexcept
    {
        if (oper >= 0 && oper < kNumGenerators)
        {
            value[(size_t) oper] = amount;
            present[(size_t) oper] = true;
        }
    }

    void add (int oper, int amount) noexcept
    {
        if (oper >= 0 && oper < kNumGenerators)
        {
            value[(size_t) oper] += amount;
            present[(size_t) oper] = true;
        }
    }

    int operator[] (int oper) const noexcept
    {
        return value[(size_t) oper];
    }
    bool has (int oper) const noexcept
    {
        return present[(size_t) oper];
    }
};

inline GeneratorSet defaultGenerators()
{
    GeneratorSet gens;

    // Only the non-zero defaults need saying; the array is already zeroed, and
    // zero is right for every address offset, tuning and send.
    gens.value[(size_t) kGenInitialFilterFc] = 13500; // wide open
    gens.value[(size_t) kGenDelayModEnv] = -12000;
    gens.value[(size_t) kGenAttackModEnv] = -12000;
    gens.value[(size_t) kGenHoldModEnv] = -12000;
    gens.value[(size_t) kGenDecayModEnv] = -12000;
    gens.value[(size_t) kGenReleaseModEnv] = -12000;
    gens.value[(size_t) kGenDelayVolEnv] = -12000;
    gens.value[(size_t) kGenAttackVolEnv] = -12000;
    gens.value[(size_t) kGenHoldVolEnv] = -12000;
    gens.value[(size_t) kGenDecayVolEnv] = -12000;
    gens.value[(size_t) kGenReleaseVolEnv] = -12000;
    gens.value[(size_t) kGenKeyRange] = 0x7f00; // 0..127, packed lo | hi << 8
    gens.value[(size_t) kGenVelRange] = 0x7f00;
    gens.value[(size_t) kGenScaleTuning] = 100;
    gens.value[(size_t) kGenOverridingRootKey] = -1; // means "the sample's own"

    return gens;
}

/** Whether a preset-level generator is one the format lets a preset offset.

    The specification's list, and it is short for a reason: a preset may colour
    an instrument, not redefine what it plays. Letting `sampleID` through at
    preset level would let a preset point at another sample entirely, and
    letting the address offsets through would move a loop point the instrument
    already validated.
*/
inline bool isOffsetableAtPresetLevel (int oper) noexcept
{
    switch (oper)
    {
        case kGenInstrument:
        case kGenSampleID:
        case kGenKeyRange:
        case kGenVelRange:
        case kGenKeynum:
        case kGenVelocity:
        case kGenSampleModes:
        case kGenExclusiveClass:
        case kGenOverridingRootKey:
        case kGenStartAddrsOffset:
        case kGenEndAddrsOffset:
        case kGenStartloopAddrsOffset:
        case kGenEndloopAddrsOffset:
        case kGenStartAddrsCoarseOffset:
        case kGenEndAddrsCoarseOffset:
        case kGenStartloopAddrsCoarseOffset:
        case kGenEndloopAddrsCoarseOffset: return false;

        default: return true;
    }
}

// --- the file's fixed-width records ------------------------------------------

constexpr size_t kPresetHeaderBytes = 38;
constexpr size_t kBagBytes = 4;
constexpr size_t kGeneratorBytes = 4;
constexpr size_t kInstrumentHeaderBytes = 22;
constexpr size_t kSampleHeaderBytes = 46;
constexpr size_t kNameBytes = 20;

struct PresetHeader
{
    juce::String name;
    int program = 0;
    int bank = 0;
    int bagIndex = 0;
};

struct Bag
{
    int generatorIndex = 0;
};

struct GeneratorRecord
{
    int oper = 0;
    juce::uint16 amount = 0;

    int asSigned() const noexcept
    {
        return (int) (juce::int16) amount;
    }
    int asUnsigned() const noexcept
    {
        return (int) amount;
    }
    int rangeLow() const noexcept
    {
        return (int) (amount & 0xff);
    }
    int rangeHigh() const noexcept
    {
        return (int) ((amount >> 8) & 0xff);
    }
};

struct InstrumentHeader
{
    juce::String name;
    int bagIndex = 0;
};

struct SampleHeader
{
    juce::String name;
    juce::uint32 start = 0, end = 0, loopStart = 0, loopEnd = 0, sampleRate = 44100;
    int originalPitch = 60;
    int correctionCents = 0;
    int type = 1;
};

/** The whole of the pdta list, as spans into the file's own bytes. */
struct Chunks
{
    Span smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
};

// ==============================================================================

struct SoundFontReader
{
    Span file;
    juce::StringArray& warnings;
    Chunks chunks;

    SoundFontReader (Span f, juce::StringArray& w)
        : file (f)
        , warnings (w)
    {
    }

    void warn (const juce::String& message)
    {
        // Bounded: a font that is wrong in one way is usually wrong in that way
        // thousands of times, and a warning list longer than the font is not a
        // diagnosis, it is a second problem.
        if (warnings.size() < 32)
            warnings.add (message);
        else if (warnings.size() == 32)
            warnings.add (tr (StringId::warning_andMore));
    }

    // --- structure ------------------------------------------------------------

    /** Walks the RIFF tree and records where the eight chunks we need live.
        Returns false if the file is not a soundfont at all. */
    bool findChunks()
    {
        if (! file.has (0, 12) || memcmp (file.data, "RIFF", 4) != 0
            || memcmp (file.data + 8, "sfbk", 4) != 0)
        {
            warn ("Not a SoundFont file: no RIFF/sfbk header.");
            return false;
        }

        const auto declared = (size_t) readU32 (file.data + 4);
        const auto end = juce::jmin (file.size, declared + 8);

        if (declared + 8 > file.size)
            warn ("The file is shorter than its own RIFF header says; reading what is there.");

        size_t pos = 12;

        while (pos + 8 <= end)
        {
            const auto* header = file.data + pos;
            const auto size = (size_t) readU32 (header + 4);
            const auto body = pos + 8;

            if (body + size > end)
            {
                warn ("A chunk runs past the end of the file; stopping there.");
                break;
            }

            if (memcmp (header, "LIST", 4) == 0 && size >= 4)
                readList ({ file.data + body + 4, size - 4 }, file.data + body);

            // Chunks are word-aligned, and an odd size carries a pad byte.
            pos = body + size + (size & 1);
        }

        return true;
    }

    void readList (Span body, const juce::uint8* listType)
    {
        const auto isPdta = memcmp (listType, "pdta", 4) == 0;
        const auto isSdta = memcmp (listType, "sdta", 4) == 0;

        if (! isPdta && ! isSdta)
            return;

        size_t pos = 0;

        while (pos + 8 <= body.size)
        {
            const auto* header = body.data + pos;
            const auto size = (size_t) readU32 (header + 4);

            if (pos + 8 + size > body.size)
            {
                warn ("A chunk inside a LIST runs past its end; stopping there.");
                break;
            }

            const Span span { body.data + pos + 8, size };

            if (memcmp (header, "smpl", 4) == 0)
                chunks.smpl = span;
            else if (memcmp (header, "sm24", 4) == 0)
                warn ("This font carries 24-bit sample data; the low bytes are ignored.");
            else if (memcmp (header, "phdr", 4) == 0)
                chunks.phdr = span;
            else if (memcmp (header, "pbag", 4) == 0)
                chunks.pbag = span;
            else if (memcmp (header, "pgen", 4) == 0)
                chunks.pgen = span;
            else if (memcmp (header, "inst", 4) == 0)
                chunks.inst = span;
            else if (memcmp (header, "ibag", 4) == 0)
                chunks.ibag = span;
            else if (memcmp (header, "igen", 4) == 0)
                chunks.igen = span;
            else if (memcmp (header, "shdr", 4) == 0)
                chunks.shdr = span;
            else if (memcmp (header, "pmod", 4) == 0 || memcmp (header, "imod", 4) == 0)
            {
                // 10 bytes per record and a terminal one, so anything above a
                // single record means the font actually uses modulators.
                if (size > 10)
                    warn ("This font uses modulators, which are not applied.");
            }

            pos += 8 + size + (size & 1);
        }
    }

    // --- fixed-width record arrays -------------------------------------------

    template <typename Record, size_t RecordBytes, typename Read>
    std::vector<Record> readRecords (Span span, Read read) const
    {
        std::vector<Record> records;

        if (span.data == nullptr)
            return records;

        const auto count = span.size / RecordBytes;
        records.reserve (count);

        for (size_t i = 0; i < count; ++i)
            records.push_back (read (span.data + i * RecordBytes));

        return records;
    }

    std::vector<PresetHeader> readPresetHeaders() const
    {
        return readRecords<PresetHeader, kPresetHeaderBytes> (
            chunks.phdr,
            [] (const juce::uint8* p)
            {
                return PresetHeader { readName (p, kNameBytes), (int) readU16 (p + 20),
                                      (int) readU16 (p + 22), (int) readU16 (p + 24) };
            });
    }

    std::vector<InstrumentHeader> readInstrumentHeaders() const
    {
        return readRecords<InstrumentHeader, kInstrumentHeaderBytes> (
            chunks.inst, [] (const juce::uint8* p)
            { return InstrumentHeader { readName (p, kNameBytes), (int) readU16 (p + 20) }; });
    }

    std::vector<Bag> readBags (Span span) const
    {
        return readRecords<Bag, kBagBytes> (span, [] (const juce::uint8* p)
                                            { return Bag { (int) readU16 (p) }; });
    }

    std::vector<GeneratorRecord> readGenerators (Span span) const
    {
        return readRecords<GeneratorRecord, kGeneratorBytes> (
            span, [] (const juce::uint8* p)
            { return GeneratorRecord { (int) readU16 (p), readU16 (p + 2) }; });
    }

    std::vector<SampleHeader> readSampleHeaders() const
    {
        return readRecords<SampleHeader, kSampleHeaderBytes> (
            chunks.shdr,
            [] (const juce::uint8* p)
            {
                SampleHeader h;
                h.name = readName (p, kNameBytes);
                h.start = readU32 (p + 20);
                h.end = readU32 (p + 24);
                h.loopStart = readU32 (p + 28);
                h.loopEnd = readU32 (p + 32);
                h.sampleRate = readU32 (p + 36);
                h.originalPitch = (int) p[40];
                h.correctionCents = (int) (juce::int8) p[41];
                h.type = (int) readU16 (p + 44);
                return h;
            });
    }
};

} // namespace dew::sf2
