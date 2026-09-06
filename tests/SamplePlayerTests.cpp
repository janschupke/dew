#include <catch2/catch_approx.hpp>
#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/SamplePlayer.h"

using namespace dew;

namespace
{

constexpr double rate = 48000.0;

/** A snapshot with one audio channel holding `audio`, and one clip on it. */
EngineSnapshot snapshotWith (std::shared_ptr<juce::AudioBuffer<float>> audio, int startBar = 0,
                             int lengthBars = 1)
{
    EngineSnapshot snapshot;
    snapshot.tempoBpm = 120.0;
    snapshot.stepsPerBeat = 4;

    // The map has to AGREE with the tempo beside it. A hand-built snapshot gets
    // the shared default - which is 128bpm - and a clip's placement is read from
    // the map while these tests compute their positions from the tempo, so
    // leaving them disagreeing would put every clip after the first bar in the
    // wrong place and only show up in the tests that render one.
    snapshot.tempoMap = std::make_shared<const TempoMap> (
        TempoMap::constant (snapshot.tempoBpm, snapshot.stepsPerBeat));

    ChannelSnapshot channel;
    channel.id = 1;
    channel.source = InstrumentType::audio;
    channel.sample.sourceSampleRate = rate;
    channel.sample.startSample = 0;
    channel.sample.endSample = audio->getNumSamples();
    channel.audio = std::move (audio);

    snapshot.channels.push_back (channel);

    ClipSnapshot clip;
    clip.channelIndex = 0;
    clip.startStep = (startBar) * 16;
    clip.lengthSteps = (lengthBars) * 16;
    snapshot.clips.push_back (clip);

    return snapshot;
}

std::shared_ptr<juce::AudioBuffer<float>> constant (int numSamples, float value)
{
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
        buffer->setSample (0, i, value);

    return buffer;
}

/** A buffer whose sample n is n, so a read position is readable from the output. */
std::shared_ptr<juce::AudioBuffer<float>> counting (int numSamples)
{
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
        buffer->setSample (0, i, (float) i);

    return buffer;
}

float peakOf (const std::vector<float>& block)
{
    auto peak = 0.0f;

    for (const auto value : block)
        peak = juce::jmax (peak, std::abs (value));

    return peak;
}

/** Samples per step at 120 bpm, 4 steps per beat, 48 kHz. */
constexpr double samplesPerStep = rate * 60.0 / 120.0 / 4.0;

/** Renders one channel's clips out of a whole snapshot.

    SamplePlayer takes what it reads rather than a snapshot and an index now -
    the settings, the audio, the clips and the metre - so that an instrument
    module can call it without the engine's whole state in its interface. This
    unpacks a snapshot exactly as the engine does, so these tests still say what
    they said.
*/
void renderChannel (float* out, int numSamples, const EngineSnapshot& snapshot, int channelIndex,
                    double positionSteps, double sps, double engineRate)
{
    if (channelIndex < 0 || channelIndex >= (int) snapshot.channels.size())
        return;

    const auto& channel = snapshot.channels[(size_t) channelIndex];

    if (channel.source != InstrumentType::audio || channel.audio == nullptr)
        return;

    // The tests speak in steps and a samples-per-step; the player now speaks in
    // samples and a map. Converting here keeps every existing expectation about
    // where a clip starts and what it plays exactly where it was.
    SamplePlayer::renderAdd (out, numSamples, channel.sample, *channel.audio,
                             { snapshot.clips.data(), snapshot.clips.size() }, channelIndex,
                             (juce::int64) std::llround (positionSteps * sps), *snapshot.tempoMap,
                             engineRate);
}

} // namespace

TEST_CASE ("an audio clip sounds at its own bar and nowhere else", "[audio][sampler]")
{
    const auto snapshot = snapshotWith (constant (200000, 0.5f), 2, 1);

    std::vector<float> block (512, 0.0f);

    // Bar 0: before the clip.
    renderChannel (block.data(), 512, snapshot, 0, 0.0, samplesPerStep, rate);
    REQUIRE (peakOf (block) == Catch::Approx (0.0f));

    // Bar 2, where it starts.
    std::fill (block.begin(), block.end(), 0.0f);
    renderChannel (block.data(), 512, snapshot, 0, 32.0, samplesPerStep, rate);
    REQUIRE (peakOf (block) == Catch::Approx (0.5f));

    // Bar 4: past the end of a one-bar clip.
    std::fill (block.begin(), block.end(), 0.0f);
    renderChannel (block.data(), 512, snapshot, 0, 64.0, samplesPerStep, rate);
    REQUIRE (peakOf (block) == Catch::Approx (0.0f));
}

TEST_CASE ("seeking into the middle of a clip lands at the right offset", "[audio][sampler]")
{
    // The reason the player is position-driven rather than event-triggered: a
    // seek is the same arithmetic as playing forwards into the clip.
    const auto snapshot = snapshotWith (counting (200000), 0, 4);

    std::vector<float> block (16, 0.0f);

    // Four steps in. The clip starts at bar 0, so the read offset is exactly
    // four steps' worth of frames.
    renderChannel (block.data(), 16, snapshot, 0, 4.0, samplesPerStep, rate);

    REQUIRE (block[0] == Catch::Approx (4.0 * samplesPerStep).margin (1.0));
    REQUIRE (block[1] == Catch::Approx (4.0 * samplesPerStep + 1.0).margin (1.0));
}

TEST_CASE ("a one-shot stops at the end of its audio", "[audio][sampler]")
{
    // 100 frames of audio in a clip a bar long: everything after frame 100 is
    // silence, not a wrap and not the last sample held.
    auto snapshot = snapshotWith (constant (100, 1.0f), 0, 1);

    std::vector<float> block (512, 0.0f);
    renderChannel (block.data(), 512, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (block[50] == Catch::Approx (1.0f));
    REQUIRE (block[400] == Catch::Approx (0.0f));
}

TEST_CASE ("looping fills the clip instead of stopping", "[audio][sampler]")
{
    auto snapshot = snapshotWith (constant (100, 1.0f), 0, 1);
    snapshot.channels[0].sample.loop = true;

    std::vector<float> block (512, 0.0f);
    renderChannel (block.data(), 512, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (block[400] == Catch::Approx (1.0f));
}

TEST_CASE ("trim silences what it excludes", "[audio][sampler]")
{
    // Loud for the first half, silent for the second. Trimming to the second
    // half must produce silence, which no clamp or fallback could fake.
    auto audio = std::make_shared<juce::AudioBuffer<float>> (1, 1000);

    for (int i = 0; i < 1000; ++i)
        audio->setSample (0, i, i < 500 ? 1.0f : 0.0f);

    auto snapshot = snapshotWith (audio, 0, 1);
    snapshot.channels[0].sample.startSample = 500;
    snapshot.channels[0].sample.endSample = 1000;

    std::vector<float> block (256, 0.0f);
    renderChannel (block.data(), 256, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (peakOf (block) == Catch::Approx (0.0f));
}

TEST_CASE ("reverse plays the region backwards", "[audio][sampler]")
{
    auto snapshot = snapshotWith (counting (1000), 0, 1);
    snapshot.channels[0].sample.reverse = true;

    std::vector<float> block (8, 0.0f);
    renderChannel (block.data(), 8, snapshot, 0, 0.0, samplesPerStep, rate);

    // Starts at the last frame and counts down.
    REQUIRE (block[0] == Catch::Approx (999.0f).margin (1.0));
    REQUIRE (block[1] < block[0]);
}

TEST_CASE ("transpose changes the read rate", "[audio][sampler]")
{
    auto snapshot = snapshotWith (counting (200000), 0, 4);

    // An octave up is twice the rate, so the same output sample reads twice as
    // far into the source.
    snapshot.channels[0].sample.pitchRatio = 2.0f;

    std::vector<float> block (8, 0.0f);
    renderChannel (block.data(), 8, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (block[1] == Catch::Approx (2.0f).margin (0.01));
    REQUIRE (block[4] == Catch::Approx (8.0f).margin (0.01));
}

TEST_CASE ("a device running at another rate does not transpose the sample", "[audio][sampler]")
{
    // The source is 48 kHz; the device is 96 kHz. Reading at 1.0 would play the
    // take an octave low, which is why the rate conversion is separate from the
    // user's transpose rather than folded into it on the message thread.
    auto snapshot = snapshotWith (counting (200000), 0, 4);

    std::vector<float> block (8, 0.0f);
    renderChannel (block.data(), 8, snapshot, 0, 0.0, samplesPerStep * 2.0, 96000.0);

    REQUIRE (block[2] == Catch::Approx (1.0f).margin (0.01));
}

TEST_CASE ("a fade in ramps from silence", "[audio][sampler]")
{
    auto snapshot = snapshotWith (constant (100000, 1.0f), 0, 1);
    snapshot.channels[0].sample.fadeInSamples = 1000;

    std::vector<float> block (1200, 0.0f);
    renderChannel (block.data(), 1200, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (block[0] == Catch::Approx (0.0f).margin (0.01));
    REQUIRE (block[500] == Catch::Approx (0.5f).margin (0.02));
    REQUIRE (block[1100] == Catch::Approx (1.0f).margin (0.01));
}

TEST_CASE ("a muted playlist track silences an audio clip", "[audio][sampler]")
{
    auto snapshot = snapshotWith (constant (100000, 1.0f), 0, 1);
    snapshot.clips[0].trackAudible = false;

    std::vector<float> block (256, 0.0f);
    renderChannel (block.data(), 256, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (peakOf (block) == Catch::Approx (0.0f));
}

TEST_CASE ("a channel with no audio renders nothing rather than crashing", "[audio][sampler]")
{
    EngineSnapshot snapshot;

    ChannelSnapshot channel;
    channel.source = InstrumentType::audio;
    snapshot.channels.push_back (channel);

    ClipSnapshot clip;
    clip.channelIndex = 0;
    snapshot.clips.push_back (clip);

    std::vector<float> block (256, 0.0f);
    renderChannel (block.data(), 256, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (peakOf (block) == Catch::Approx (0.0f));
}

TEST_CASE ("an audio-only project is not reported as silent", "[audio][sampler]")
{
    // isSilent drives dew_render's exit code, so an arrangement of recordings
    // with no notes in it must not be called "nothing to play".
    const auto snapshot = snapshotWith (constant (1000, 0.5f), 0, 1);

    REQUIRE (! snapshot.isSilent());
}

TEST_CASE ("an audio clip gives the arrangement its length", "[audio][sampler]")
{
    // songLengthSteps drives how much dew_render renders and how far the song
    // loops. It counted pattern and automation clips only, so an arrangement of
    // nothing but recordings had no length and rendered as "nothing to play".
    const auto snapshot = snapshotWith (constant (1000, 0.5f), 2, 3);

    REQUIRE (snapshot.songLengthSteps() == (2 + 3) * snapshot.stepsPerBar());
}

TEST_CASE ("adding is not replacing: two clips of one channel sum", "[audio][sampler]")
{
    auto snapshot = snapshotWith (constant (100000, 0.25f), 0, 1);

    // A second clip over the same bar. Overlapping takes should sum rather than
    // one silently winning.
    ClipSnapshot second;
    second.channelIndex = 0;
    second.startStep = (0) * 16;
    second.lengthSteps = (1) * 16;
    snapshot.clips.push_back (second);

    std::vector<float> block (256, 0.0f);
    renderChannel (block.data(), 256, snapshot, 0, 0.0, samplesPerStep, rate);

    REQUIRE (block[10] == Catch::Approx (0.5f));
}
