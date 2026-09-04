#include <catch2/catch_test_macros.hpp>

#include "io/SoundFontFile.h"

using namespace dew;

/** The reader, against real files rather than one we wrote ourselves.

    A hand-built fixture proves the parser reads what the fixture builder
    writes, which is the same person's idea of the format twice. This walks a
    directory of fonts made by real authoring tools, and it is what the cut list
    in SoundFontFile.h was measured against.

    Skipped, not failed, when the directory is not given: the fonts are a
    personal library outside the repository and CI will never have them.

        DEW_SOUNDFONT_CORPUS=~/path/to/fonts ./build/ci/tests/dew_tests "[corpus]"
*/
namespace
{

juce::File corpusDirectory()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("DEW_SOUNDFONT_CORPUS", {});

    if (path.isEmpty())
        return {};

    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

juce::Array<juce::File> corpusFonts()
{
    juce::Array<juce::File> found;
    const auto directory = corpusDirectory();

    if (! directory.isDirectory())
        return found;

    for (const auto& entry :
         juce::RangedDirectoryIterator (directory, true, "*", juce::File::findFiles))
        if (SoundFontFile::isSoundFont (entry.getFile()))
            found.add (entry.getFile());

    return found;
}

} // namespace

TEST_CASE ("every font in the corpus parses into something playable", "[soundfont][corpus]")
{
    const auto fonts = corpusFonts();

    if (fonts.isEmpty())
    {
        WARN ("DEW_SOUNDFONT_CORPUS is not set to a directory of .sf2 files; skipping.");
        return;
    }

    int presets = 0, regions = 0, stereo = 0, looped = 0, filtered = 0, exclusive = 0;

    for (const auto& file : fonts)
    {
        INFO ("font: " << file.getFullPathName());

        const auto result = SoundFontFile::read (file);

        REQUIRE (result.isValid());
        REQUIRE_FALSE (result.font.presets.empty());

        const auto poolSize = (juce::uint32) result.font.pcm.size();

        for (const auto& preset : result.font.presets)
        {
            ++presets;

            INFO ("preset: " << preset.name);
            REQUIRE_FALSE (preset.regions.empty());
            REQUIRE (preset.bank >= 0);
            REQUIRE (preset.program >= 0);

            for (const auto& region : preset.regions)
            {
                ++regions;

                // The whole point of resolving at load time: a voice reads these
                // every sample and must never re-validate them.
                REQUIRE (region.end > region.start);
                REQUIRE (region.end <= poolSize);
                REQUIRE (region.lowKey <= region.highKey);
                REQUIRE (region.lowVelocity <= region.highVelocity);
                REQUIRE (region.rootKey >= 0);
                REQUIRE (region.rootKey <= 127);
                REQUIRE (region.sampleRate > 0.0);

                if (region.loop != SoundFontLoop::none)
                {
                    ++looped;
                    REQUIRE (region.loopEnd > region.loopStart);
                    REQUIRE (region.loopStart >= region.start);
                    REQUIRE (region.loopEnd <= region.end);
                }

                if (std::abs (region.pan) > 0.01f)
                    ++stereo;

                if (region.filterCutoffHz < 19000.0f)
                    ++filtered;

                if (region.exclusiveClass != 0)
                    ++exclusive;
            }
        }
    }

    WARN ("corpus: " << fonts.size() << " fonts, " << presets << " presets, " << regions
                     << " regions (" << stereo << " panned, " << looped << " looped, " << filtered
                     << " filtered, " << exclusive << " exclusive-class)");

    REQUIRE (presets >= fonts.size());
    REQUIRE (regions > 0);
}

TEST_CASE ("the corpus only warns about what the reader deliberately cuts", "[soundfont][corpus]")
{
    const auto fonts = corpusFonts();

    if (fonts.isEmpty())
        return;

    // A warning naming anything but a documented cut means the reader met
    // something in a real file it did not expect, which is exactly what this
    // corpus is here to find.
    const juce::StringArray expected {
        "modulators",         "modLfoToPitch",       "vibLfoToPitch",
        "modEnvToPitch",      "modLfoToFilterFc",    "modLfoToVolume",
        "chorusEffectsSend",  "reverbEffectsSend",   "delayModLFO",
        "freqModLFO",         "delayVibLFO",         "freqVibLFO",
        "keynumToModEnvHold", "keynumToModEnvDecay", "24-bit"
    };

    juce::StringArray unexpected;

    for (const auto& file : fonts)
    {
        for (const auto& warning : SoundFontFile::read (file).warnings)
        {
            auto known = false;

            for (const auto& term : expected)
                known = known || warning.contains (term);

            if (! known && ! unexpected.contains (warning))
                unexpected.add (file.getFileName() + ": " + warning);
        }
    }

    INFO ("unexpected warnings:\n" << unexpected.joinIntoString ("\n"));
    CHECK (unexpected.isEmpty());
}
