#include <catch2/catch_test_macros.hpp>

#include "engine/Sequencer.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

/** The tests speak in SAMPLES PER STEP, which is what Sequencer::collect took
    before there was a tempo map. A constant map at a fixed rate is the same
    thing said the other way round, and keeps every existing expectation - the
    step boundaries, the offsets, the durations - exactly where it was.
*/
constexpr double kTestSampleRate = 48000.0;

dew::TempoMap mapFor (double samplesPerStep)
{
    // stepsPerBeat of 1, so seconds-per-step IS 60/bpm and the bpm that produces
    // this many samples per step falls straight out.
    return dew::TempoMap::constant (60.0 * kTestSampleRate / samplesPerStep, 1);
}

/** A snapshot with one channel and one pattern whose notes are at the given steps. */
EngineSnapshot snapshotWithSteps (const std::vector<int>& steps, int patternLength = 16,
                                  int noteLengthSteps = 1)
{
    EngineSnapshot s;
    s.tempoBpm = 120.0;
    s.stepsPerBeat = 4;

    ChannelSnapshot channel;
    channel.id = 1;
    channel.mixerTrackIndex = 0;
    s.channels.push_back (channel);

    MixerTrackSnapshot mixer;
    mixer.id = 1;
    s.mixerTracks.push_back (mixer);

    PatternSnapshot pattern;
    pattern.id = 1;
    pattern.lengthSteps = patternLength;

    for (auto step : steps)
    {
        NoteSnapshot note;
        note.channelIndex = 0;
        note.step = step;
        note.lengthSteps = noteLengthSteps;
        note.pitch = 60;
        pattern.notes.push_back (note);
    }

    s.patterns.push_back (pattern);
    return s;
}

std::vector<NoteTrigger> collectOver (const EngineSnapshot& snapshot, Transport::Mode mode,
                                      juce::int64 fromSample, juce::int64 toSample, int blockSize,
                                      double samplesPerStep, int patternIndex,
                                      std::vector<juce::int64>& absoluteOffsets)
{
    std::vector<NoteTrigger> all;
    std::vector<NoteTrigger> block;

    for (auto position = fromSample; position < toSample; position += blockSize)
    {
        const auto thisBlock = (int) juce::jmin ((juce::int64) blockSize, toSample - position);

        Sequencer::collect (snapshot, mode, position, thisBlock, mapFor (samplesPerStep),
                            kTestSampleRate, patternIndex, block);

        for (const auto& trigger : block)
        {
            all.push_back (trigger);
            absoluteOffsets.push_back (position + trigger.sampleOffset);
        }
    }

    return all;
}

} // namespace

TEST_CASE ("notes fire at their step boundary", "[sequencer]")
{
    const auto snapshot = snapshotWithSteps ({ 0, 4, 8, 12 });
    const auto samplesPerStep = 6000.0; // 120 bpm, 16ths, 48 kHz

    std::vector<juce::int64> offsets;
    const auto triggers = collectOver (snapshot, Transport::Mode::pattern, 0, 16 * 6000, 512,
                                       samplesPerStep, 0, offsets);

    REQUIRE (triggers.size() == 4);
    REQUIRE (offsets == std::vector<juce::int64> { 0, 24000, 48000, 72000 });
}

TEST_CASE ("a note landing inside a block gets the right offset", "[sequencer]")
{
    const auto snapshot = snapshotWithSteps ({ 1 }); // step 1 = sample 6000
    std::vector<NoteTrigger> out;

    // Block 5900..6412 contains sample 6000 at offset 100.
    Sequencer::collect (snapshot, Transport::Mode::pattern, 5900, 512, mapFor (6000.0),
                        kTestSampleRate, 0, out);

    REQUIRE (out.size() == 1);
    REQUIRE (out[0].sampleOffset == 100);
}

TEST_CASE ("no note is fired twice or dropped across block boundaries", "[sequencer]")
{
    const auto snapshot = snapshotWithSteps (
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });

    // Deliberately awkward block sizes that do not divide the step length.
    for (int blockSize : { 1, 7, 64, 333, 512, 1024, 8192 })
    {
        std::vector<juce::int64> offsets;
        const auto triggers = collectOver (snapshot, Transport::Mode::pattern, 0, 16 * 6000,
                                           blockSize, 6000.0, 0, offsets);

        INFO ("block size " << blockSize);
        REQUIRE (triggers.size() == 16);

        for (size_t i = 0; i < offsets.size(); ++i)
            REQUIRE (offsets[i] == (juce::int64) i * 6000);
    }
}

TEST_CASE ("a pattern repeats when the transport passes its end", "[sequencer]")
{
    const auto snapshot = snapshotWithSteps ({ 0 }, 16);

    std::vector<juce::int64> offsets;
    // Three pattern lengths' worth of time.
    const auto triggers = collectOver (snapshot, Transport::Mode::pattern, 0, 3 * 16 * 6000, 512,
                                       6000.0, 0, offsets);

    REQUIRE (triggers.size() == 3);
    REQUIRE (offsets == std::vector<juce::int64> { 0, 96000, 192000 });
}

TEST_CASE ("a note carries its duration so the voice can release itself", "[sequencer]")
{
    const auto snapshot = snapshotWithSteps ({ 0 }, 16, 4);
    std::vector<NoteTrigger> out;

    Sequencer::collect (snapshot, Transport::Mode::pattern, 0, 512, mapFor (6000.0),
                        kTestSampleRate, 0, out);

    REQUIRE (out.size() == 1);
    REQUIRE (out[0].durationSamples == 4 * 6000);
}

TEST_CASE ("song mode plays clips at their bar positions", "[sequencer]")
{
    auto snapshot = snapshotWithSteps ({ 0 }, 16);
    snapshot.stepsPerBeat = 4; // 16 steps per bar

    ClipSnapshot first;
    first.patternIndex = 0;
    first.startBar = 0;
    first.lengthBars = 1;
    ClipSnapshot third;
    third.patternIndex = 0;
    third.startBar = 2;
    third.lengthBars = 1;
    snapshot.clips = { first, third };

    std::vector<juce::int64> offsets;
    const auto triggers = collectOver (snapshot, Transport::Mode::song, 0, 4 * 16 * 6000, 512,
                                       6000.0, -1, offsets);

    // Bar 0 and bar 2 only; nothing in bar 1 or 3.
    REQUIRE (triggers.size() == 2);
    REQUIRE (offsets == std::vector<juce::int64> { 0, 2 * 16 * 6000 });
}

TEST_CASE ("a clip longer than its pattern repeats the pattern to fill", "[sequencer]")
{
    auto snapshot = snapshotWithSteps ({ 0 }, 16);

    ClipSnapshot clip;
    clip.patternIndex = 0;
    clip.startBar = 0;
    clip.lengthBars = 3; // three bars of a one-bar pattern
    snapshot.clips = { clip };

    std::vector<juce::int64> offsets;
    const auto triggers = collectOver (snapshot, Transport::Mode::song, 0, 3 * 16 * 6000, 512,
                                       6000.0, -1, offsets);

    REQUIRE (triggers.size() == 3);
    REQUIRE (offsets == std::vector<juce::int64> { 0, 96000, 192000 });
}

TEST_CASE ("the material length matches the mode", "[sequencer]")
{
    auto snapshot = snapshotWithSteps ({ 0 }, 32);

    REQUIRE (Sequencer::materialLengthSteps (snapshot, Transport::Mode::pattern, 0) == 32);

    // An empty playlist has nothing to play.
    REQUIRE (Sequencer::materialLengthSteps (snapshot, Transport::Mode::song, 0) == 0);

    ClipSnapshot clip;
    clip.patternIndex = 0;
    clip.startBar = 1;
    clip.lengthBars = 2;
    snapshot.clips = { clip };

    // Ends at bar 3 -> 3 bars * 16 steps.
    REQUIRE (Sequencer::materialLengthSteps (snapshot, Transport::Mode::song, 0) == 3 * 16);
}

TEST_CASE ("the demo project schedules notes on every channel", "[sequencer][demo]")
{
    const auto snapshot = buildSnapshot (dew::testing::fixtureProject());

    REQUIRE (! snapshot.isSilent());
    REQUIRE (snapshot.channels.size() == 4);
    REQUIRE (snapshot.songLengthSteps() == 4 * 16);

    std::vector<juce::int64> offsets;
    const auto samplesPerStep = Transport::samplesPerStepFor (snapshot.tempoBpm,
                                                              snapshot.stepsPerBeat, 44100.0);

    const auto triggers = collectOver (snapshot, Transport::Mode::song, 0,
                                       (juce::int64) (samplesPerStep * snapshot.songLengthSteps()),
                                       512, samplesPerStep, -1, offsets);

    std::vector<bool> channelPlayed (snapshot.channels.size(), false);

    for (const auto& trigger : triggers)
        channelPlayed[(size_t) trigger.channelIndex] = true;

    for (size_t i = 0; i < channelPlayed.size(); ++i)
    {
        INFO ("channel " << i << " (" << snapshot.channels[i].id << ") never plays");
        REQUIRE (channelPlayed[i]);
    }
}
