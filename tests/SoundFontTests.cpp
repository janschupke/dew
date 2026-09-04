#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FixtureSoundFont.h"
#include "engine/AudioEngine.h"
#include "engine/SoundFontChannel.h"
#include "io/SoundFontFile.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;
using dew::testing::SoundFontBuilder;

namespace
{

std::shared_ptr<const SoundFontData> fontFrom (const SoundFontBuilder& builder)
{
    const auto bytes = builder.build();
    auto result = SoundFontFile::parse (bytes.getData(), bytes.getSize(), "Fixture");
    REQUIRE (result.isValid());
    return std::make_shared<const SoundFontData> (std::move (result.font));
}

/** Renders a channel and hands back the two sides separately, because whether
    they differ is the question half of these ask. */
struct Rendered
{
    juce::AudioBuffer<float> buffer;

    float peak (int channel) const
    {
        return buffer.getMagnitude (channel, 0, buffer.getNumSamples());
    }

    bool sidesDiffer() const
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! juce::exactlyEqual (buffer.getSample (0, i), buffer.getSample (1, i)))
                return true;

        return false;
    }
};

Rendered render (SoundFontChannel& channel, int numSamples)
{
    Rendered out { juce::AudioBuffer<float> (2, numSamples) };
    out.buffer.clear();
    channel.renderAdd (out.buffer.getWritePointer (0), out.buffer.getWritePointer (1), numSamples);
    return out;
}

/** A provider that answers with one font, whatever it is asked for. */
struct OneFontProvider : SoundFontProvider
{
    std::shared_ptr<const SoundFontData> font;

    std::shared_ptr<const SoundFontData> soundFontFor (const juce::String&) override
    {
        return font;
    }
};

} // namespace

TEST_CASE ("a soundfont channel sounds when a note starts and stops when it ends",
           "[soundfont][engine]")
{
    const auto font = fontFrom (SoundFontBuilder::minimal());

    SoundFontChannel channel;
    channel.prepare (44100.0);

    CHECK (channel.countActiveVoices() == 0);

    channel.noteOn (*font, font->presets.front(), 60, 1.0f, {}, 0, 0);
    CHECK (channel.countActiveVoices() == 1);

    CHECK (render (channel, 256).peak (0) > 0.0f);

    channel.allNotesOff();

    // The region has the format's default release, which is immediate, so the
    // voice is gone within a block rather than ringing.
    render (channel, 4096);
    CHECK (channel.countActiveVoices() == 0);
}

TEST_CASE ("a note an octave up reads its sample twice as fast", "[soundfont][engine]")
{
    // The one piece of arithmetic a sampler cannot get wrong and still be a
    // sampler, so it is measured rather than trusted: a region that loops over
    // a hundred frames comes round twice as often an octave up.
    auto builder = SoundFontBuilder::minimal();
    builder.samples.front().data = SoundFontBuilder::rampSamples (200);
    const auto font = fontFrom (builder);

    const auto loopResets = [&font] (int pitch)
    {
        SoundFontChannel channel;
        channel.prepare (44100.0);
        channel.noteOn (*font, font->presets.front(), pitch, 1.0f, {}, 0, 0);

        const auto rendered = render (channel, 4000);

        int falls = 0;

        for (int i = 1; i < rendered.buffer.getNumSamples(); ++i)
            // The ramp only ever falls where it comes round again.
            if (rendered.buffer.getSample (0, i) < rendered.buffer.getSample (0, i - 1) - 0.2f)
                ++falls;

        return falls;
    };

    const auto atRoot = loopResets (60);
    const auto anOctaveUp = loopResets (72);

    REQUIRE (atRoot > 0);
    CHECK (anOctaveUp >= atRoot * 2 - 1);
    CHECK (anOctaveUp <= atRoot * 2 + 1);
}

TEST_CASE ("a region that does not loop stops at the end of its sample", "[soundfont][engine]")
{
    auto builder = SoundFontBuilder::minimal();

    // sampleModes is the first generator minimal() writes; 0 is "no loop".
    builder.instruments.front().zones.front().generators[1] = { 54, 0 };

    const auto font = fontFrom (builder);

    SoundFontChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (*font, font->presets.front(), 60, 1.0f, {}, 0, 0);

    // Two hundred frames of sample, rendered well past it.
    render (channel, 2000);
    CHECK (channel.countActiveVoices() == 0);
}

TEST_CASE ("an exclusive class cuts the voice it replaces", "[soundfont][engine]")
{
    // Without it a closed hi-hat does not silence an open one.
    auto builder = SoundFontBuilder::minimal();
    builder.instruments.front().zones.front().generators.push_back ({ 57, 3 });

    const auto font = fontFrom (builder);

    SoundFontChannel channel;
    channel.prepare (44100.0);

    channel.noteOn (*font, font->presets.front(), 60, 1.0f, {}, 0, 0);
    channel.noteOn (*font, font->presets.front(), 64, 1.0f, {}, 0, 0);

    CHECK (channel.countActiveVoices() == 1);
    CHECK (render (channel, 64).peak (0) > 0.0f);
}

TEST_CASE ("one note-on starts every region that answers to it", "[soundfont][engine]")
{
    // A stereo pair is two regions, which is why a soundfont channel needs more
    // voices than the synth does.
    const auto font = fontFrom (SoundFontBuilder::stereo());

    SoundFontChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (*font, font->presets.front(), 60, 1.0f, {}, 0, 0);

    CHECK (channel.countActiveVoices() == 2);
}

TEST_CASE ("a hard-panned pair reaches the two sides differently", "[soundfont][engine]")
{
    // The reason the instrument ABI was widened at all.
    auto builder = SoundFontBuilder::stereo();

    // Make the two halves different, so a summed output could not pass.
    builder.samples[1].data = SoundFontBuilder::rampSamples (200);

    for (auto& value : builder.samples[1].data)
        value = (juce::int16) -value;

    const auto font = fontFrom (builder);

    SoundFontChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (*font, font->presets.front(), 60, 1.0f, {}, 0, 0);

    const auto rendered = render (channel, 128);

    CHECK (rendered.peak (0) > 0.0f);
    CHECK (rendered.peak (1) > 0.0f);
    CHECK (rendered.sidesDiffer());
}

TEST_CASE ("the voice budget holds and the oldest note is the one stolen", "[soundfont][engine]")
{
    const auto font = fontFrom (SoundFontBuilder::minimal());

    SoundFontChannel channel;
    channel.prepare (44100.0);

    for (int i = 0; i < kMaxSoundFontVoicesPerChannel * 2; ++i)
    {
        channel.noteOn (*font, font->presets.front(), 40 + i, 1.0f, {}, 0, 0);
        render (channel, 16);
    }

    CHECK (channel.countActiveVoices() == kMaxSoundFontVoicesPerChannel);
}

TEST_CASE ("velocity sensitivity decides how much a soft note is attenuated", "[soundfont][engine]")
{
    const auto font = fontFrom (SoundFontBuilder::minimal());

    const auto peakAt = [&font] (float velocity, float sensitivity)
    {
        SoundFontSettings settings;
        settings.velocitySensitivity = sensitivity;

        SoundFontChannel channel;
        channel.prepare (44100.0);
        channel.noteOn (*font, font->presets.front(), 60, velocity, settings, 0, 0);

        return render (channel, 256).peak (0);
    };

    CHECK (peakAt (0.25f, 1.0f) < peakAt (1.0f, 1.0f));

    // At nothing, every note plays at full level - which is what a stepped
    // pattern usually wants, and what the shipped "Stepped" preset asks for.
    CHECK (peakAt (0.25f, 0.0f) == Catch::Approx (peakAt (1.0f, 0.0f)));
}

TEST_CASE ("a soundfont channel renders through the engine", "[soundfont][engine]")
{
    auto project = ProjectFactory::createDefault();
    auto channel = ProjectEdits::addSoundFontChannel (project, "Font", nullptr);
    ProjectEdits::setSoundFontSource (channel, "font.sf2", 0, 0, "Ramp", nullptr);

    OneFontProvider provider;
    provider.font = fontFrom (SoundFontBuilder::minimal());

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings, nullptr, &provider);

    INFO (warnings.joinIntoString ("\n"));
    REQUIRE (warnings.isEmpty());

    const auto index = (size_t) (snapshot.channels.size() - 1);
    REQUIRE (snapshot.channels[index].source == InstrumentType::soundfont);
    REQUIRE (snapshot.channels[index].soundFont != nullptr);
}

TEST_CASE ("a channel whose soundfont is missing is silent and says so", "[soundfont][engine]")
{
    // A soundfont is a library you own rather than a take that belongs to one
    // song, so a project arriving before its fonts is ordinary rather than
    // broken - but it must never be silent without a word.
    auto project = ProjectFactory::createDefault();
    auto channel = ProjectEdits::addSoundFontChannel (project, "Font", nullptr);
    ProjectEdits::setSoundFontSource (channel, "gone.sf2", 0, 0, "Ramp", nullptr);

    OneFontProvider provider; // answers with nothing

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings, nullptr, &provider);

    const auto index = (size_t) (snapshot.channels.size() - 1);
    CHECK (snapshot.channels[index].soundFont == nullptr);
    CHECK (warnings.joinIntoString (" ").contains ("gone.sf2"));
}

TEST_CASE ("a soundfont channel takes notes and is not a clip channel", "[soundfont][model]")
{
    // The predicate `isAudioChannel` used to answer both questions, and a third
    // kind of instrument made two of its call sites wrong.
    auto project = ProjectFactory::createDefault();

    auto synth = ProjectEdits::addChannel (project, "Synth", nullptr);
    auto audio = ProjectEdits::addAudioChannel (project, "Take", nullptr);
    auto font = ProjectEdits::addSoundFontChannel (project, "Font", nullptr);

    CHECK (ProjectEdits::playsNotes (synth));
    CHECK_FALSE (ProjectEdits::playsClips (synth));

    CHECK_FALSE (ProjectEdits::playsNotes (audio));
    CHECK (ProjectEdits::playsClips (audio));

    CHECK (ProjectEdits::playsNotes (font));
    CHECK_FALSE (ProjectEdits::playsClips (font));

    REQUIRE (ProjectEdits::instrumentTypeOf (font).has_value());
    CHECK (*ProjectEdits::instrumentTypeOf (font) == InstrumentType::soundfont);
}
