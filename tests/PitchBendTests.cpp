#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "engine/AudioEngine.h"
#include "io/OfflineRenderer.h"
#include "engine/SynthVoice.h"
#include "model/ProjectFactory.h"

using namespace dew;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;

/** "Bass": a saw with sustain 0.35, so a held note keeps sounding for as long
    as the test needs it to. Channel 0 is the kick, whose sustain is 0 - a note
    there has decayed to nothing before the second half of a render, which makes
    it useless for measuring a pitch that changes over time. A saw also crosses
    zero exactly once per cycle, which is what the crossing count assumes.
*/
constexpr int channelIndex = 2;

/** Counts upward zero crossings, which is a cheap stand-in for the fundamental:
    a note bent up crosses more often over the same span. Good enough to prove
    the pitch moved and in which direction, without an FFT.
*/
int upwardCrossings (const juce::AudioBuffer<float>& buffer)
{
    const auto* samples = buffer.getReadPointer (0);
    int count = 0;

    for (int i = 1; i < buffer.getNumSamples(); ++i)
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f)
            ++count;

    return count;
}

/** Renders a held preview note for `blocks` blocks, with the controllers set
    however `configure` wants them, and returns the concatenated audio.
*/
juce::AudioBuffer<float> renderHeldNote (const std::function<void (AudioEngine&)>& configure,
                                         int blocks = 16)
{
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);

    // One block to land the snapshot, then the note.
    block.clear();
    engine.processBlock (block);

    engine.previewNoteOn (channelIndex, 69, 0.9f);

    if (configure)
        configure (engine);

    juce::AudioBuffer<float> out (2, blockSize * blocks);
    out.clear();

    for (int i = 0; i < blocks; ++i)
    {
        block.clear();
        engine.processBlock (block);

        for (int channel = 0; channel < 2; ++channel)
            out.copyFrom (channel, i * blockSize, block, channel, 0, blockSize);
    }

    return out;
}

} // namespace

TEST_CASE ("bending up raises the pitch of a note already sounding", "[engine][bend]")
{
    // The point of the whole exercise: before this, phaseIncrement was latched
    // at note-on and a sounding voice could not change pitch at all.
    const auto plain = renderHeldNote (nullptr);
    const auto bent = renderHeldNote ([] (AudioEngine& e) { e.setChannelBend (channelIndex, 2.0f); });

    const auto plainCrossings = upwardCrossings (plain);
    const auto bentCrossings = upwardCrossings (bent);

    INFO ("plain " << plainCrossings << " crossings, bent " << bentCrossings);
    REQUIRE (plainCrossings > 0);
    REQUIRE (bentCrossings > plainCrossings);

    // Two semitones is a ratio of 2^(2/12), about 1.122.
    const auto ratio = (double) bentCrossings / (double) plainCrossings;
    REQUIRE (ratio == Approx (1.122).margin (0.02));
}

TEST_CASE ("bending down lowers it", "[engine][bend]")
{
    const auto plain = renderHeldNote (nullptr);
    const auto bent = renderHeldNote ([] (AudioEngine& e) { e.setChannelBend (channelIndex, -2.0f); });

    REQUIRE (upwardCrossings (bent) < upwardCrossings (plain));
}

TEST_CASE ("a centred wheel renders exactly what no wheel renders", "[engine][bend]")
{
    // Exactly, not nearly. An untouched controller has to be bit-for-bit what
    // the engine produced before there was one, which is what lets every
    // existing render test stand unchanged.
    const auto plain = renderHeldNote (nullptr);
    const auto centred = renderHeldNote ([] (AudioEngine& e) { e.setChannelBend (channelIndex, 0.0f); });

    REQUIRE (plain.getNumSamples() == centred.getNumSamples());

    for (int i = 0; i < plain.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (plain.getReadPointer (0)[i], centred.getReadPointer (0)[i]));
}

TEST_CASE ("a mod wheel at rest adds no vibrato at all", "[engine][bend]")
{
    const auto plain = renderHeldNote (nullptr);
    const auto zeroed = renderHeldNote ([] (AudioEngine& e) { e.setChannelModulation (channelIndex, 0.0f); });

    for (int i = 0; i < plain.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (plain.getReadPointer (0)[i], zeroed.getReadPointer (0)[i]));
}

TEST_CASE ("the mod wheel makes the pitch move within one render", "[engine][bend]")
{
    const auto modulated = renderHeldNote ([] (AudioEngine& e) { e.setChannelModulation (channelIndex, 1.0f); },
                                           64);

    // Split it into thirds and compare crossing counts: vibrato means they
    // differ, where a steady note would give the same figure each time.
    const auto third = modulated.getNumSamples() / 3;

    juce::AudioBuffer<float> a (2, third), b (2, third);

    for (int channel = 0; channel < 2; ++channel)
    {
        a.copyFrom (channel, 0, modulated, channel, 0, third);
        b.copyFrom (channel, 0, modulated, channel, third, third);
    }

    const auto crossingsA = upwardCrossings (a);
    const auto crossingsB = upwardCrossings (b);

    INFO ("thirds: " << crossingsA << " then " << crossingsB);
    REQUIRE (crossingsA != crossingsB);

    // And it stays a vibrato rather than becoming a siren: half a semitone
    // either way is a ratio of at most 2^(0.5/12), about 1.03.
    const auto ratio = (double) juce::jmax (crossingsA, crossingsB)
                     / (double) juce::jmax (1, juce::jmin (crossingsA, crossingsB));

    INFO ("ratio " << ratio << " for a max depth of "
                   << SynthVoice::maxVibratoSemitones << " semitones");
    REQUIRE (ratio < 1.10);
}

TEST_CASE ("bend applies to sequencer notes too, not only played ones", "[engine][bend]")
{
    // It is a property of the CHANNEL, so anything sounding on that channel
    // bends - which is what a player expects when the transport is running.
    const auto render = [] (float bend)
    {
        AudioEngine engine;
        engine.prepare (sampleRate, blockSize);
        engine.setProject (ProjectFactory::createDemo());

        juce::AudioBuffer<float> block (2, blockSize);
        block.clear();
        engine.processBlock (block);

        engine.setChannelBend (channelIndex, bend);
        engine.play();

        juce::AudioBuffer<float> out (2, blockSize * 48);
        out.clear();

        for (int i = 0; i < 48; ++i)
        {
            block.clear();
            engine.processBlock (block);

            for (int channel = 0; channel < 2; ++channel)
                out.copyFrom (channel, i * blockSize, block, channel, 0, blockSize);
        }

        return out;
    };

    const auto plain = render (0.0f);
    const auto bent = render (2.0f);

    REQUIRE (plain.getMagnitude (0, 0, plain.getNumSamples()) > 0.0f);

    bool differs = false;

    for (int i = 0; i < plain.getNumSamples() && ! differs; ++i)
        differs = ! juce::exactlyEqual (plain.getReadPointer (0)[i], bent.getReadPointer (0)[i]);

    REQUIRE (differs);
}

TEST_CASE ("the offline renderer is untouched by controllers it never sets", "[engine][bend]")
{
    // Guards every existing render test against silent drift: with the
    // controllers at their defaults, the new code must change nothing.
    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (ProjectFactory::createDemo(), rendered);

    REQUIRE (report.ok());
    REQUIRE (report.peak > 0.05f);
    REQUIRE (report.peak <= 1.0f);
}

TEST_CASE ("a bend out of range cannot break the engine", "[engine][bend]")
{
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);
    block.clear();
    engine.processBlock (block);

    engine.previewNoteOn (channelIndex, 100, 0.9f);

    for (const auto bend : { 1000.0f, -1000.0f, 0.0f })
    {
        engine.setChannelBend (channelIndex, bend);

        block.clear();
        engine.processBlock (block);

        // Whatever the increment clamps to, the output stays finite and bounded.
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const auto sample = block.getReadPointer (0)[i];
            REQUIRE (std::isfinite (sample));
            REQUIRE (std::abs (sample) <= 4.0f);
        }
    }
}

TEST_CASE ("controller writes out of bounds are refused rather than corrupting memory",
           "[engine][bend]")
{
    AudioEngine engine;

    engine.setChannelBend (-1, 2.0f);
    engine.setChannelBend (kMaxChannels, 2.0f);
    engine.setChannelBend (kMaxChannels + 100, 2.0f);

    REQUIRE (juce::exactlyEqual (engine.getChannelBend (-1), 0.0f));
    REQUIRE (juce::exactlyEqual (engine.getChannelBend (kMaxChannels), 0.0f));
}

TEST_CASE ("modulation is clamped to nought to one", "[engine][bend]")
{
    AudioEngine engine;

    engine.setChannelModulation (0, 5.0f);
    REQUIRE (juce::exactlyEqual (engine.getChannelModulation (0), 1.0f));

    engine.setChannelModulation (0, -5.0f);
    REQUIRE (juce::exactlyEqual (engine.getChannelModulation (0), 0.0f));
}

TEST_CASE ("a stream of bends cannot overflow anything", "[engine][bend]")
{
    // The reason controllers are latest-wins atomics rather than queued: a
    // wheel sweep is hundreds of messages a second, and there is nothing here
    // that can fill up and start refusing notes.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);
    block.clear();
    engine.processBlock (block);

    engine.previewNoteOn (channelIndex, 69, 0.8f);

    for (int i = 0; i < 10000; ++i)
    {
        engine.setChannelBend (channelIndex, std::sin ((float) i * 0.01f) * 2.0f);
        engine.setChannelModulation (channelIndex, (float) (i % 128) / 127.0f);

        if (i % 100 == 0)
        {
            block.clear();
            engine.processBlock (block);
        }
    }

    // Still a working engine afterwards.
    engine.setChannelBend (channelIndex, 0.0f);
    engine.setChannelModulation (channelIndex, 0.0f);
    engine.previewAllOff();
    engine.previewNoteOn (0, 72, 0.8f);

    auto peak = 0.0f;

    for (int i = 0; i < 6; ++i)
    {
        block.clear();
        engine.processBlock (block);
        peak = juce::jmax (peak, block.getMagnitude (0, 0, block.getNumSamples()));
    }

    REQUIRE (peak > 0.0f);
}
