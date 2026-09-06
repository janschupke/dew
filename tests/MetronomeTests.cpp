#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "engine/Metronome.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"

#include <vector>

using namespace dew;
using Catch::Approx;

namespace
{

constexpr double kRate = 48000.0;

/** 120 bpm at four steps to the beat: a beat is exactly 24000 samples, so every
    expectation below is an integer and a click that is one sample out is a
    failure rather than a rounding argument. The default project's pattern is
    empty, which is what makes any non-silent sample a CLICK. */
constexpr double kBpm = 120.0;
constexpr int kSamplesPerBeat = 24000;

juce::ValueTree silentProject()
{
    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::tempoBpm, kBpm, nullptr);
    return project;
}

/** Renders `numBlocks` blocks into one buffer, so onsets can be read as
    absolute sample positions rather than per block. */
juce::AudioBuffer<float> renderBlocks (AudioEngine& engine, int blockSize, int numBlocks)
{
    juce::AudioBuffer<float> whole (2, blockSize * numBlocks);
    juce::AudioBuffer<float> block (2, blockSize);

    for (int i = 0; i < numBlocks; ++i)
    {
        engine.processBlock (block);

        for (int channel = 0; channel < 2; ++channel)
            whole.copyFrom (channel, i * blockSize, block, channel, 0, blockSize);
    }

    return whole;
}

/** Where each burst of sound starts, and how loud it is. */
struct Onset
{
    int at;
    float peak;
};

/** A click is a decaying SINE, so it passes through zero every half period and
    a scan that ended a burst at the first quiet sample would count one onset
    per cycle. A burst therefore only ends after a quiet RUN longer than any
    cycle in it - and shorter than the gap between clicks, which is a beat. */
constexpr int kQuietRun = 512;

std::vector<Onset> onsetsOf (const juce::AudioBuffer<float>& buffer)
{
    constexpr auto floorLevel = 0.001f;

    std::vector<Onset> onsets;
    const auto* data = buffer.getReadPointer (0);
    auto quiet = kQuietRun;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto level = std::abs (data[i]);

        if (level <= floorLevel)
        {
            ++quiet;
            continue;
        }

        if (quiet >= kQuietRun)
            onsets.push_back ({ i, level });
        else
            onsets.back().peak = juce::jmax (onsets.back().peak, level);

        quiet = 0;
    }

    return onsets;
}

/** Where the i-th click's first AUDIBLE sample is.

    One past the beat, and exactly one: the voice is struck at a phase of zero,
    so the sample on the beat itself is the sine's own zero crossing. Written
    out rather than absorbed into a tolerance, because the whole point of these
    numbers is that a click landing a sample late is a failure.
*/
constexpr int firstSampleOf (int beat)
{
    return beat * kSamplesPerBeat + 1;
}

AudioEngine& preparedEngine (AudioEngine& engine, int blockSize)
{
    engine.prepare (kRate, blockSize);

    juce::StringArray warnings;
    engine.setProject (silentProject(), &warnings);
    return engine;
}

} // namespace

TEST_CASE ("a count-in is a whole number of bars in the project's own metre", "[engine][metronome]")
{
    // Four beats at 120 bpm is two seconds; three beats is one and a half. The
    // 3/4 case is the one that matters: finishRecording once folded four beats
    // into a constant, and every take in 3/4 came out a quarter too long.
    CHECK (countInSamplesFor (1, Meter { 4, 4, 4 }, kBpm, kRate) == 4 * kSamplesPerBeat);
    CHECK (countInSamplesFor (1, Meter { 4, 3, 4 }, kBpm, kRate) == 3 * kSamplesPerBeat);

    // The denominator names a beat; it does not change how long one is.
    CHECK (countInSamplesFor (1, Meter { 4, 4, 8 }, kBpm, kRate)
           == countInSamplesFor (1, Meter { 4, 4, 4 }, kBpm, kRate));

    CHECK (countInSamplesFor (2, Meter { 4, 4, 4 }, kBpm, kRate) == 8 * kSamplesPerBeat);

    // A count-in of no bars is still a bar: there is no such thing as counting
    // in for nothing, and a zero would be a silent pre-roll with no clicks.
    CHECK (countInSamplesFor (0, Meter { 4, 4, 4 }, kBpm, kRate) == 4 * kSamplesPerBeat);
}

TEST_CASE ("a click lands on every beat, with an accent on the bar", "[engine][metronome]")
{
    constexpr auto blockSize = 256;

    AudioEngine engine;
    preparedEngine (engine, blockSize);
    engine.setMetronomeEnabled (true);
    engine.play();

    // Four beats and a little, so the fourth click is inside the buffer.
    const auto rendered = renderBlocks (engine, blockSize, 390);
    const auto onsets = onsetsOf (rendered);

    // The control case: a scan that found nothing must not pass.
    INFO ("onsets: " << (int) onsets.size());
    REQUIRE (onsets.size() >= 4);

    for (int i = 0; i < 4; ++i)
    {
        INFO ("click " << i << " at " << onsets[(size_t) i].at);
        CHECK (onsets[(size_t) i].at == firstSampleOf (i));
    }

    // The downbeat is the loud one, and the three after it are each other's.
    CHECK (onsets[0].peak > onsets[1].peak);
    CHECK (onsets[1].peak == Approx (onsets[2].peak).margin (0.001));
}

TEST_CASE ("the same clicks come back at a different block size", "[engine][metronome]")
{
    // 24000 divides neither 256 nor 384, so in both runs every beat after the
    // first falls INSIDE a block rather than on its edge - which is the case a
    // per-block scan gets wrong.
    const auto onsetsAt = [] (int blockSize, int numBlocks)
    {
        AudioEngine engine;
        preparedEngine (engine, blockSize);
        engine.setMetronomeEnabled (true);
        engine.play();

        std::vector<int> positions;

        for (const auto& onset : onsetsOf (renderBlocks (engine, blockSize, numBlocks)))
            if (onset.at < 3 * kSamplesPerBeat)
                positions.push_back (onset.at);

        return positions;
    };

    const auto small = onsetsAt (256, 285);
    const auto large = onsetsAt (384, 190);

    REQUIRE (small.size() == 3);
    CHECK (small == large);
}

TEST_CASE ("the metronome off changes not one sample", "[engine][metronome]")
{
    constexpr auto blockSize = 256;
    constexpr auto numBlocks = 200;

    AudioEngine untouched;
    preparedEngine (untouched, blockSize);
    untouched.play();

    AudioEngine explicitlyOff;
    preparedEngine (explicitlyOff, blockSize);
    explicitlyOff.setMetronomeEnabled (false);
    explicitlyOff.play();

    const auto a = renderBlocks (untouched, blockSize, numBlocks);
    const auto b = renderBlocks (explicitlyOff, blockSize, numBlocks);

    REQUIRE (a.getNumSamples() == b.getNumSamples());

    for (int i = 0; i < a.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (a.getReadPointer (0)[i], b.getReadPointer (0)[i]));

    // And the other half: switching it ON must change the buffer, or the
    // comparison above passes for a metronome that never sounds.
    AudioEngine clicking;
    preparedEngine (clicking, blockSize);
    clicking.setMetronomeEnabled (true);
    clicking.play();

    CHECK (renderBlocks (clicking, blockSize, numBlocks).getMagnitude (0, blockSize * numBlocks)
           > 0.01f);
}

TEST_CASE ("the click is not in the master meter", "[engine][metronome]")
{
    constexpr auto blockSize = 256;

    AudioEngine engine;
    preparedEngine (engine, blockSize);
    engine.setMetronomeEnabled (true);
    engine.play();

    const auto rendered = renderBlocks (engine, blockSize, 200);

    // Audible in the buffer...
    REQUIRE (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.01f);

    // ...and invisible to the meter and the scope, because it is summed after
    // both. That is the whole placement rule, stated.
    CHECK (engine.readAndClearMasterPeak() == 0.0f);
}

TEST_CASE ("a count-in holds the transport still and clicks through it",
           "[engine][metronome][countin]")
{
    constexpr auto blockSize = 256;
    const auto countIn = countInSamplesFor (1, Meter { 4, 4, 4 }, kBpm, kRate);

    AudioEngine engine;
    preparedEngine (engine, blockSize);

    // Somewhere other than zero, so "the playhead did not move" cannot be
    // satisfied by a rewind.
    engine.setPlayheadSteps (4.0);

    juce::AudioBuffer<float> block (2, blockSize);
    engine.processBlock (block);

    const auto startedAt = engine.getPlayheadSteps();
    REQUIRE (startedAt > 0.0);

    engine.playWithCountIn (countIn);
    REQUIRE (engine.isCountingIn());

    const auto blocks = (int) (countIn / blockSize);
    const auto during = renderBlocks (engine, blockSize, blocks);

    // Still where it was, and still counting: the budget is spent in whole
    // blocks, so a count-in that is not a multiple of one costs the remainder.
    CHECK (engine.getPlayheadSteps() == Approx (startedAt));

    // Four clicks, one a beat, and the FIRST is the accented one - the count-in
    // is a bar, so its downbeat is where it begins.
    const auto onsets = onsetsOf (during);
    INFO ("clicks during the count-in: " << (int) onsets.size());
    REQUIRE (onsets.size() == 4);
    CHECK (onsets[0].peak > onsets[1].peak);

    // Evenly spaced, and the last one exactly a beat before the end - which is
    // the property anchoring to the downbeat buys.
    for (size_t i = 1; i < onsets.size(); ++i)
        CHECK (onsets[i].at - onsets[i - 1].at == kSamplesPerBeat);

    CHECK ((int) countIn - (onsets.back().at - 1) == kSamplesPerBeat);

    // One more block finishes the budget and the transport starts moving.
    engine.processBlock (block);
    CHECK_FALSE (engine.isCountingIn());

    engine.processBlock (block);
    CHECK (engine.getPlayheadSteps() > startedAt);
}

TEST_CASE ("stopping cancels a count-in", "[engine][metronome][countin]")
{
    constexpr auto blockSize = 256;

    AudioEngine engine;
    preparedEngine (engine, blockSize);

    engine.playWithCountIn (countInSamplesFor (1, Meter { 4, 4, 4 }, kBpm, kRate));
    REQUIRE (engine.isCountingIn());

    engine.stop();
    CHECK_FALSE (engine.isCountingIn());

    // And the next play is a play, not the rest of a count-in somebody
    // abandoned.
    engine.play();
    CHECK_FALSE (engine.isCountingIn());
    CHECK (engine.getCountInRemainingSamples() == 0);
}

TEST_CASE ("a render carries no clicks", "[engine][metronome][render]")
{
    // It holds by CONSTRUCTION - OfflineRenderer builds its own AudioEngine and
    // the flag defaults to off - which is exactly why it is worth pinning: the
    // way to lose it, a metronome option threaded into RenderOptions, would
    // look like a feature.
    AudioEngine live;
    live.prepare (kRate, 256);
    live.setMetronomeEnabled (true);
    live.play();

    juce::AudioBuffer<float> a, b;

    // PATTERN mode: the default project has no playlist clips, so a song render
    // has no span to plan and fails before it reaches an engine at all. One
    // empty pattern is sixteen steps of material and renders silence.
    RenderOptions options;
    options.mode = Transport::Mode::pattern;
    options.patternId = 1;
    options.sampleRate = kRate;

    const auto first = OfflineRenderer::renderToBuffer (silentProject(), a, options);
    const auto second = OfflineRenderer::renderToBuffer (silentProject(), b, options);

    REQUIRE (first.ok());
    REQUIRE (second.ok());
    REQUIRE (a.getNumSamples() == b.getNumSamples());
    REQUIRE (a.getNumSamples() > 0);

    for (int i = 0; i < a.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (a.getReadPointer (0)[i], b.getReadPointer (0)[i]));

    // An empty project renders silence, and a click would be the only thing in
    // it - so this is the assertion that would fail if one ever reached a file.
    CHECK (a.getMagnitude (0, a.getNumSamples()) == 0.0f);
}
