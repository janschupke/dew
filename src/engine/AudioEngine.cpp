#include "AudioEngine.h"

namespace dew
{

AudioEngine::AudioEngine()
{
    channels.resize (kMaxChannels);
    triggers.reserve (256);
}

void AudioEngine::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = juce::jmax (1, maximumBlockSize);

    transport.prepare (currentSampleRate);

    for (auto& channel : channels)
        channel.prepare (currentSampleRate);

    // Everything the audio thread might need, allocated once.
    channelBuffers.setSize (kMaxChannels, currentBlockSize);
    mixerBuffers.setSize (kMaxMixerTracks * 2, currentBlockSize);
    channelBuffers.clear();
    mixerBuffers.clear();

    triggers.reserve (256);
}

void AudioEngine::releaseResources()
{
    for (auto& channel : channels)
        channel.reset();

    channelBuffers.setSize (0, 0);
    mixerBuffers.setSize (0, 0);
}

void AudioEngine::setProject (const juce::ValueTree& project, juce::StringArray* warnings)
{
    publish (buildSnapshot (project, warnings));
}

void AudioEngine::publish (EngineSnapshot snapshot)
{
    bridge.publish (std::move (snapshot));
}

void AudioEngine::play()
{
    playing.store (true);
}

void AudioEngine::stop()
{
    playing.store (false);
}

void AudioEngine::rewind()
{
    rewindRequested.store (true);
}

void AudioEngine::setMode (Transport::Mode mode)
{
    requestedMode.store (mode);
}

double AudioEngine::getPlayheadSteps() const noexcept
{
    const auto sps = Transport::samplesPerStepFor (transport.getTempo(),
                                                   transport.getStepsPerBeat(),
                                                   currentSampleRate);

    return sps > 0.0 ? (double) playheadSamples.load() / sps : 0.0;
}

void AudioEngine::applySnapshotIfChanged (const EngineSnapshot& snapshot) noexcept
{
    if (snapshot.generation == appliedGeneration)
        return;

    appliedGeneration = snapshot.generation;
    transport.setTempo (snapshot.tempoBpm, snapshot.stepsPerBeat);
}

void AudioEngine::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    // One latch per block; the reference is stable until the next acquire().
    const auto& snapshot = bridge.acquire();
    applySnapshotIfChanged (snapshot);

    const auto mode = requestedMode.load();
    transport.setMode (mode);

    const auto patternIndex = snapshot.patternIndexForId (requestedPatternId.load());

    const auto loopSteps = Sequencer::loopLengthSteps (snapshot, mode, patternIndex);
    transport.setLoopLengthSteps (loopSteps);

    if (rewindRequested.exchange (false))
    {
        transport.rewind();

        for (auto& channel : channels)
            channel.reset();
    }

    const auto isPlayingNow = playing.load();

    // Even when stopped, voices keep rendering so a note released at the moment
    // of stopping finishes its tail instead of clicking off.
    if (isPlayingNow && loopSteps > 0)
    {
        Sequencer::collect (snapshot, mode, transport.getPositionSamples(), numSamples,
                            transport.samplesPerStep(), patternIndex, triggers);
    }
    else
    {
        triggers.clear();
    }

    const auto numChannels = juce::jmin ((int) snapshot.channels.size(), kMaxChannels);
    const auto numMixerTracks = juce::jmin ((int) snapshot.mixerTracks.size(), kMaxMixerTracks);

    channelBuffers.clear (0, numSamples);
    mixerBuffers.clear (0, numSamples);

    // --- trigger notes -------------------------------------------------------
    for (const auto& trigger : triggers)
    {
        if (trigger.channelIndex < 0 || trigger.channelIndex >= numChannels)
            continue;

        const auto& channelSnapshot = snapshot.channels[(size_t) trigger.channelIndex];

        if (channelSnapshot.muted)
            continue;

        channels[(size_t) trigger.channelIndex].noteOn (trigger.pitch,
                                                        trigger.velocity,
                                                        channelSnapshot.osc,
                                                        channelSnapshot.amp,
                                                        trigger.durationSamples);
    }

    // --- render channels into their mixer tracks -----------------------------
    for (int i = 0; i < numChannels; ++i)
    {
        auto* mono = channelBuffers.getWritePointer (i);
        channels[(size_t) i].renderAdd (mono, numSamples);

        const auto& channelSnapshot = snapshot.channels[(size_t) i];

        if (channelSnapshot.muted)
            continue;

        const auto mixerIndex = channelSnapshot.mixerTrackIndex;

        if (mixerIndex < 0 || mixerIndex >= numMixerTracks)
            continue;

        MixerBus::addPanned (mono, numSamples,
                             channelSnapshot.volume, channelSnapshot.pan,
                             mixerBuffers.getWritePointer (mixerIndex * 2),
                             mixerBuffers.getWritePointer (mixerIndex * 2 + 1));
    }

    // --- mixer tracks into the master ----------------------------------------
    auto* outLeft = buffer.getWritePointer (0);
    auto* outRight = buffer.getWritePointer (1);

    for (int i = 0; i < numMixerTracks; ++i)
    {
        const auto& track = snapshot.mixerTracks[(size_t) i];

        if (! MixerBus::isAudible (snapshot, track))
            continue;

        float leftGain = 0.0f, rightGain = 0.0f;
        MixerBus::panGains (track.pan, leftGain, rightGain);

        juce::FloatVectorOperations::addWithMultiply (outLeft,
                                                      mixerBuffers.getReadPointer (i * 2),
                                                      track.gain * leftGain * juce::MathConstants<float>::sqrt2,
                                                      numSamples);
        juce::FloatVectorOperations::addWithMultiply (outRight,
                                                      mixerBuffers.getReadPointer (i * 2 + 1),
                                                      track.gain * rightGain * juce::MathConstants<float>::sqrt2,
                                                      numSamples);
    }

    buffer.applyGain (snapshot.masterGain);

    if (isPlayingNow && loopSteps > 0)
        transport.advance (numSamples);

    playheadSamples.store (transport.getPositionSamples());
}

} // namespace dew
