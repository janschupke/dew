#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;

namespace
{

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;

/** "Bass": sustain 0.35, so a held note is still sounding when the panic
    arrives. The kick's sustain is 0 and would have decayed on its own, which
    would prove nothing. */
constexpr int channelIndex = 2;

float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getMagnitude (0, buffer.getNumSamples());
}

/** Renders one block and hands back its peak. */
float renderBlock (AudioEngine& engine, juce::AudioBuffer<float>& block)
{
    block.clear();
    engine.processBlock (block);
    return peakOf (block);
}

} // namespace

TEST_CASE ("a panic silences a note that is still sounding", "[engine][panic]")
{
    // stop() deliberately does NOT do this: a note released at the moment of
    // stopping finishes its tail instead of clicking off, and EngineTests pins
    // that. Panic is the other answer, for a stuck note or a runaway input.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);

    // One block to land the snapshot, then a note held down and never released.
    renderBlock (engine, block);
    engine.previewNoteOn (channelIndex, 69, 0.9f);

    // Two blocks, so the note is past its attack and unambiguously sounding.
    renderBlock (engine, block);
    const auto sounding = renderBlock (engine, block);
    REQUIRE (sounding > 0.01f);

    engine.panic();

    // The block that consumes the request may still hold the tail of what was
    // cut; the one after it has nothing left at all.
    renderBlock (engine, block);
    CHECK (renderBlock (engine, block) < 0.0001f);
}

TEST_CASE ("a panic drops what was already queued", "[engine][panic]")
{
    // A note-on sitting in a ring would otherwise be applied in the very block
    // that was asked to silence everything, and start a voice the person
    // pressed a button to stop.
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);
    renderBlock (engine, block);

    // Queued and then panicked BEFORE any block runs, so the event is still in
    // the ring when the request is consumed.
    engine.previewNoteOn (channelIndex, 69, 0.9f);
    engine.panic();

    CHECK (renderBlock (engine, block) < 0.0001f);
    CHECK (renderBlock (engine, block) < 0.0001f);
}

TEST_CASE ("a panic stops the transport and returns it to the start", "[engine][panic]")
{
    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (ProjectFactory::createDefault());

    juce::AudioBuffer<float> block (2, blockSize);

    engine.play();

    for (int i = 0; i < 8; ++i)
        renderBlock (engine, block);

    REQUIRE (engine.isPlaying());
    REQUIRE (engine.getPlayheadSteps() > 0.0);

    engine.panic();

    CHECK_FALSE (engine.isPlaying());

    renderBlock (engine, block);

    // The playhead AND the marker, which is what rewind means: a marker left
    // standing would show one place on the ruler and start at another.
    CHECK (engine.getPlayheadSteps() < 0.0001);
    CHECK (engine.getStartMarkerSteps() < 0.0001);
}

TEST_CASE ("a panic silences an effect tail, which a stop does not", "[engine][panic]")
{
    // The half that needs the pool: a two second reverb outlives every voice
    // that fed it, so killing the voices alone leaves the room ringing.
    auto project = ProjectFactory::createDefault();

    auto channel = project.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    auto reverb = ProjectEdits::addEffect (project, channel, "reverb", nullptr);
    REQUIRE (reverb.isValid());

    // All wet and as large as it goes, so the tail is the loudest thing here.
    reverb.setProperty (ids::mix, 1.0, nullptr);
    reverb.setProperty (ids::roomSize, 0.95, nullptr);

    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setProject (project);

    juce::AudioBuffer<float> block (2, blockSize);
    renderBlock (engine, block);

    engine.previewNoteOn (0, 60, 1.0f);

    for (int i = 0; i < 8; ++i)
        renderBlock (engine, block);

    engine.previewNoteOff (0, 60);

    // The voice is gone but the room is not: this is the state a plain stop
    // leaves, and the reason this test exists.
    for (int i = 0; i < 4; ++i)
        renderBlock (engine, block);

    REQUIRE (renderBlock (engine, block) > 0.0001f);

    engine.panic();

    renderBlock (engine, block);
    CHECK (renderBlock (engine, block) < 0.0001f);
}
