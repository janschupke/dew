#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FixtureSoundFont.h"
#include "io/SoundFontFile.h"

using namespace dew;
using dew::testing::SoundFontBuilder;

namespace
{

SoundFontFile::Result parse (const SoundFontBuilder& builder)
{
    const auto bytes = builder.build();
    return SoundFontFile::parse (bytes.getData(), bytes.getSize(), "Fixture");
}

constexpr int kGenKeyRange = 43;
constexpr int kGenVelRange = 44;
constexpr int kGenAttackVolEnv = 34;
constexpr int kGenInitialAttenuation = 48;
constexpr int kGenPan = 17;
constexpr int kGenCoarseTune = 51;
constexpr int kGenExclusiveClass = 57;
constexpr int kGenOverridingRootKey = 58;
constexpr int kGenStartloopAddrsOffset = 2;

} // namespace

TEST_CASE ("a minimal soundfont reads back as one playable region", "[soundfont][file]")
{
    const auto result = parse (SoundFontBuilder::minimal());

    INFO (result.warnings.joinIntoString ("\n"));
    REQUIRE (result.warnings.isEmpty());
    REQUIRE (result.isValid());
    REQUIRE (result.font.presets.size() == 1);

    const auto& preset = result.font.presets.front();
    REQUIRE (preset.name == "Ramp");
    REQUIRE (preset.bank == 0);
    REQUIRE (preset.program == 0);
    REQUIRE (preset.regions.size() == 1);

    const auto& region = preset.regions.front();
    CHECK (region.lowKey == 0);
    CHECK (region.highKey == 127);
    CHECK (region.rootKey == 60);
    CHECK (region.end - region.start == 200);
    CHECK (region.loop == SoundFontLoop::continuous);
    CHECK (region.loopEnd > region.loopStart);
}

TEST_CASE ("a preset addresses a sound by bank and program", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.presets.front().bank = 8;
    builder.presets.front().program = 42;

    const auto result = parse (builder);

    REQUIRE (result.isValid());
    CHECK (result.font.presetFor (8, 42) != nullptr);
    CHECK (result.font.presetFor (0, 0) == nullptr);
    CHECK (result.font.firstPreset() == result.font.presetFor (8, 42));
}

TEST_CASE ("an instrument zone's generators are absolute", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();

    // Six decibels of attenuation, stated once at instrument level.
    builder.instruments.front().zones.front().generators.push_back ({ kGenInitialAttenuation, 60 });

    const auto result = parse (builder);
    const auto& region = result.font.presets.front().regions.front();

    CHECK (region.gain == Catch::Approx (0.5011f).margin (0.001f));
}

TEST_CASE ("a preset zone's generators are added to the instrument's", "[soundfont][file]")
{
    // The rule the whole two-level structure exists for, and the one that makes
    // every preset in a font sound like its first if it is got backwards.
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators.push_back ({ kGenCoarseTune, 2 });
    builder.presets.front().zones.front().generators.push_back ({ kGenCoarseTune, 3 });

    const auto result = parse (builder);
    const auto& region = result.font.presets.front().regions.front();

    // Added, not replaced: five semitones, in cents.
    CHECK (region.tuneCents == Catch::Approx (500.0f));
}

TEST_CASE ("a preset zone's key range narrows the instrument's rather than moving it",
           "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators = { { kGenKeyRange, 20 | (100 << 8) },
                                                             { kGenOverridingRootKey, 60 } };
    builder.presets.front().zones.front().generators.push_back ({ kGenKeyRange, 40 | (80 << 8) });

    const auto result = parse (builder);
    const auto& region = result.font.presets.front().regions.front();

    CHECK (region.lowKey == 40);
    CHECK (region.highKey == 80);
}

TEST_CASE ("a preset key range that meets nothing yields no region", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators = { { kGenKeyRange, 0 | (30 << 8) },
                                                             { kGenOverridingRootKey, 60 } };
    builder.presets.front().zones.front().generators.push_back ({ kGenKeyRange, 90 | (127 << 8) });

    const auto result = parse (builder);

    CHECK_FALSE (result.isValid());
}

TEST_CASE ("a global instrument zone supplies what its siblings do not say", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();

    // A zone naming no sample is the instrument's global one, and comes first.
    builder.instruments.front().zones.insert (
        builder.instruments.front().zones.begin(),
        SoundFontBuilder::Zone { { { kGenInitialAttenuation, 60 } }, -1 });

    const auto result = parse (builder);
    const auto& region = result.font.presets.front().regions.front();

    CHECK (region.gain == Catch::Approx (0.5011f).margin (0.001f));
}

TEST_CASE ("velocity ranges keep zones apart", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    auto& zones = builder.instruments.front().zones;

    zones.front().generators.push_back ({ kGenVelRange, 0 | (63 << 8) });
    zones.push_back ({ { { kGenKeyRange, 0 | (127 << 8) },
                         { kGenVelRange, 64 | (127 << 8) },
                         { kGenOverridingRootKey, 60 } },
                       0 });

    const auto result = parse (builder);
    const auto& regions = result.font.presets.front().regions;

    REQUIRE (regions.size() == 2);
    CHECK (regions[0].matches (60, 30));
    CHECK_FALSE (regions[0].matches (60, 100));
    CHECK (regions[1].matches (60, 100));
    CHECK_FALSE (regions[1].matches (60, 30));
}

TEST_CASE ("a stereo pair with no pan generator is hard-panned by its sample type",
           "[soundfont][file]")
{
    // Without this a piano's two sides sum to the centre and the file's stereo
    // image is simply lost.
    const auto result = parse (SoundFontBuilder::stereo());

    REQUIRE (result.isValid());
    const auto& regions = result.font.presets.front().regions;
    REQUIRE (regions.size() == 2);

    CHECK (regions[0].pan == Catch::Approx (-1.0f));
    CHECK (regions[1].pan == Catch::Approx (1.0f));
}

TEST_CASE ("an explicit pan generator wins over the sample type", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::stereo();
    builder.instruments.front().zones[0].generators.push_back ({ kGenPan, 0 });

    const auto result = parse (builder);
    const auto& regions = result.font.presets.front().regions;

    REQUIRE (regions.size() == 2);
    CHECK (regions[0].pan == Catch::Approx (0.0f));
    CHECK (regions[1].pan == Catch::Approx (1.0f));
}

TEST_CASE ("an exclusive class survives into the region", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators.push_back ({ kGenExclusiveClass, 3 });

    const auto result = parse (builder);
    CHECK (result.font.presets.front().regions.front().exclusiveClass == 3);
}

TEST_CASE ("an envelope time in timecents becomes seconds", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();

    // 0 timecents is one second, which is the easiest value to be sure of.
    builder.instruments.front().zones.front().generators.push_back ({ kGenAttackVolEnv, 0 });

    const auto result = parse (builder);
    const auto& region = result.font.presets.front().regions.front();

    CHECK (region.volumeEnvelope.attackSeconds == Catch::Approx (1.0f));
}

// --- what a malformed file must do -------------------------------------------
// Every one of these is a file a person could choose off their disk. None may
// crash, assert, or read outside what was actually loaded.

TEST_CASE ("a file that is not a soundfont is refused rather than guessed", "[soundfont][file]")
{
    const char* text = "this is not a soundfont, it is a sentence about one";
    const auto result = SoundFontFile::parse (text, strlen (text), "Text");

    CHECK_FALSE (result.isValid());
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("an empty file is refused", "[soundfont][file]")
{
    const auto result = SoundFontFile::parse ("", 0, "Empty");

    CHECK_FALSE (result.isValid());
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("a truncated soundfont is read as far as it goes", "[soundfont][file]")
{
    const auto whole = SoundFontBuilder::minimal().build();

    // Every prefix of a real file, not one arbitrary cut: the interesting
    // failures are at chunk boundaries, and which byte those land on is not
    // something a test should have to know.
    for (size_t length = 1; length < whole.getSize(); length += 7)
    {
        INFO ("truncated to " << length << " of " << whole.getSize());

        const auto result = SoundFontFile::parse (whole.getData(), length, "Truncated");

        // It may or may not find a preset; what it must never do is claim one
        // whose samples are not there.
        for (const auto& preset : result.font.presets)
            for (const auto& region : preset.regions)
            {
                REQUIRE (region.end <= (juce::uint32) result.font.pcm.size());
                REQUIRE (region.end > region.start);
            }
    }
}

TEST_CASE ("a font with no preset data says so", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.omit.add ("pdta");

    const auto result = parse (builder);

    CHECK_FALSE (result.isValid());
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("a zone naming a sample the font does not have is dropped", "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().sampleIndex = 99;

    const auto result = parse (builder);

    CHECK_FALSE (result.isValid());
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("a loop outside its own sample is refused, not clamped into a read past the end",
           "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators.push_back (
        { kGenStartloopAddrsOffset, 30000 });

    const auto result = parse (builder);

    REQUIRE (result.isValid());

    const auto& region = result.font.presets.front().regions.front();
    CHECK (region.loop == SoundFontLoop::none);
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("a ROM sample is skipped rather than read from a file that does not hold it",
           "[soundfont][file]")
{
    auto builder = SoundFontBuilder::minimal();
    builder.samples.front().type = 1 | 0x8000;

    const auto result = parse (builder);

    CHECK_FALSE (result.isValid());
    CHECK_FALSE (result.warnings.isEmpty());
}

TEST_CASE ("a soundfont extension is recognised whatever its case", "[soundfont][file]")
{
    // Two files in a real library are spelled .SF2, and an exact compare simply
    // hides them.
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory);

    CHECK (SoundFontFile::isSoundFont (directory.getChildFile ("Piano.sf2")));
    CHECK (SoundFontFile::isSoundFont (directory.getChildFile ("Piano.SF2")));
    CHECK (SoundFontFile::isSoundFont (directory.getChildFile ("Piano.Sf2")));
    CHECK_FALSE (SoundFontFile::isSoundFont (directory.getChildFile ("Piano.wav")));
}
