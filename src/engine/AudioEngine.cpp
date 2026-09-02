#include "AudioEngine.h"

#include <limits>

namespace dew
{

AudioEngine::AudioEngine()
{
    channels.resize (kMaxChannels);
    triggers.reserve (256);

    effectUnits.reserve (kMaxEffectUnits);

    for (int i = 0; i < kMaxEffectUnits; ++i)
        effectUnits.push_back (std::make_unique<EffectUnit>());

    effectUnitTypes.assign (kMaxEffectUnits, -1);
    activeAutomation.reserve (kMaxAutomations);
}

void AudioEngine::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = juce::jmax (1, maximumBlockSize);

    transport.prepare (currentSampleRate);
    signalTap.setSampleRate (currentSampleRate);

    for (auto& channel : channels)
        channel.prepare (currentSampleRate);

    // Everything the audio thread might need, allocated once.
    channelBuffers.setSize (kMaxChannels, currentBlockSize);
    mixerBuffers.setSize (kMaxMixerTracks * 2, currentBlockSize);
    channelStereo.setSize (2, currentBlockSize);
    channelBuffers.clear();
    mixerBuffers.clear();
    channelStereo.clear();

    for (auto& unit : effectUnits)
        unit->prepare (currentSampleRate, currentBlockSize);

    effectUnitTypes.assign (kMaxEffectUnits, -1);

    triggers.reserve (256);
}

void AudioEngine::releaseResources()
{
    for (auto& channel : channels)
        channel.reset();

    channelBuffers.setSize (0, 0);
    mixerBuffers.setSize (0, 0);
    channelStereo.setSize (0, 0);

    signalTap.reset();
}

void AudioEngine::setProject (const juce::ValueTree& project, juce::StringArray* warnings)
{
    publish (buildSnapshot (project, warnings));
}

void AudioEngine::publish (EngineSnapshot snapshot)
{
    bridge.publish (std::move (snapshot));
}

bool AudioEngine::previewNoteOn (int channelIndex, int pitch, float velocity) noexcept
{
    return previewQueue.push ({ PreviewEvent::Kind::noteOn, channelIndex,
                                juce::jlimit (0, 127, pitch), juce::jlimit (0.0f, 1.0f, velocity) });
}

bool AudioEngine::previewNoteOff (int channelIndex, int pitch) noexcept
{
    return previewQueue.push ({ PreviewEvent::Kind::noteOff, channelIndex,
                                juce::jlimit (0, 127, pitch), 0.0f });
}

bool AudioEngine::previewAllOff() noexcept
{
    return previewQueue.push ({ PreviewEvent::Kind::allOff, 0, 0, 0.0f });
}

bool AudioEngine::midiNoteOn (int channelIndex, int pitch, float velocity) noexcept
{
    return midiQueue.push ({ PreviewEvent::Kind::noteOn, channelIndex,
                             juce::jlimit (0, 127, pitch), juce::jlimit (0.0f, 1.0f, velocity) });
}

bool AudioEngine::midiNoteOff (int channelIndex, int pitch) noexcept
{
    return midiQueue.push ({ PreviewEvent::Kind::noteOff, channelIndex,
                             juce::jlimit (0, 127, pitch), 0.0f });
}

bool AudioEngine::midiAllOff() noexcept
{
    return midiQueue.push ({ PreviewEvent::Kind::allOff, 0, 0, 0.0f });
}

void AudioEngine::setChannelBend (int channelIndex, float semitones) noexcept
{
    if (channelIndex < 0 || channelIndex >= kMaxChannels)
        return;

    channelBend[(size_t) channelIndex].store (semitones, std::memory_order_relaxed);
}

void AudioEngine::setChannelModulation (int channelIndex, float amount) noexcept
{
    if (channelIndex < 0 || channelIndex >= kMaxChannels)
        return;

    channelModulation[(size_t) channelIndex].store (juce::jlimit (0.0f, 1.0f, amount),
                                                    std::memory_order_relaxed);
}

float AudioEngine::getChannelBend (int channelIndex) const noexcept
{
    if (channelIndex < 0 || channelIndex >= kMaxChannels)
        return 0.0f;

    return channelBend[(size_t) channelIndex].load (std::memory_order_relaxed);
}

float AudioEngine::getChannelModulation (int channelIndex) const noexcept
{
    if (channelIndex < 0 || channelIndex >= kMaxChannels)
        return 0.0f;

    return channelModulation[(size_t) channelIndex].load (std::memory_order_relaxed);
}

void AudioEngine::resetControllers() noexcept
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        channelBend[(size_t) i].store (0.0f, std::memory_order_relaxed);
        channelModulation[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
}

void AudioEngine::applyPreviewEvent (const EngineSnapshot& snapshot, const PreviewEvent& event,
                                     int numChannels) noexcept
{
    if (event.kind == PreviewEvent::Kind::allOff)
    {
        for (auto& channel : channels)
            channel.allNotesOff();

        return;
    }

    if (event.channelIndex < 0 || event.channelIndex >= numChannels)
        return;

    auto& channel = channels[(size_t) event.channelIndex];

    if (event.kind == PreviewEvent::Kind::noteOff)
    {
        channel.noteOff (event.pitch);
        return;
    }

    // A preview note has no duration to run out - it lasts until the user
    // lets go - so it is triggered with one long enough that the release
    // always comes first.
    const auto& channelSnapshot = snapshot.channels[(size_t) event.channelIndex];
    channel.noteOn (event.pitch, event.velocity, channelSnapshot.osc, channelSnapshot.amp,
                    std::numeric_limits<int>::max());
}

void AudioEngine::drainPreviewQueue (const EngineSnapshot& snapshot) noexcept
{
    const auto numChannels = juce::jmin ((int) snapshot.channels.size(), kMaxChannels);

    PreviewEvent event;

    // Both rings, one consumer. Order between them is not meaningful - they are
    // fed by different threads - but order WITHIN each is, and each ring
    // preserves its own.
    while (previewQueue.pop (event))
        applyPreviewEvent (snapshot, event, numChannels);

    while (midiQueue.pop (event))
        applyPreviewEvent (snapshot, event, numChannels);
}

void AudioEngine::recordPeak (std::atomic<float>& slot, const float* left, const float* right,
                              int numSamples, float scale) noexcept
{
    auto peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

    peak *= scale;

    // Keep the loudest seen since the last read: a meter must not miss a
    // transient just because it fell between two message-thread ticks.
    auto current = slot.load (std::memory_order_relaxed);

    while (peak > current
           && ! slot.compare_exchange_weak (current, peak, std::memory_order_release,
                                            std::memory_order_relaxed))
    {
    }
}

float AudioEngine::readAndClearTrackPeak (int trackIndex) noexcept
{
    if (trackIndex < 0 || trackIndex >= kMaxMixerTracks)
        return 0.0f;

    return trackPeaks[(size_t) trackIndex].exchange (0.0f, std::memory_order_acquire);
}

float AudioEngine::readAndClearMasterPeak() noexcept
{
    return masterPeak.exchange (0.0f, std::memory_order_acquire);
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

    // Also zeroed here, on the calling thread, so the reset is immediate and
    // does not depend on a device being open. audioHost.start() failing is
    // non-fatal - it only puts a message in the status bar - and without this
    // line processBlock never runs, so the position would stay wherever it
    // stopped forever.
    //
    // The cost is one block of race: a processBlock already in flight can
    // re-store the pre-rewind position. The next block consumes
    // rewindRequested and stores zero again, so it corrects itself within a
    // few milliseconds, which is below what anyone can see.
    playheadSamples.store (0);
}

void AudioEngine::setPlayheadSteps (double steps)
{
    const auto clamped = juce::jmax (0.0, steps);

    seekToSteps.store (clamped);
    seekRequested.store (true);

    // Reflected immediately, for the same reason rewind() does it: the UI
    // should follow the pointer, not the next audio block - and a device that
    // is not calling back would otherwise never move at all.
    const auto sps = Transport::samplesPerStepFor (transport.getTempo(),
                                                   transport.getStepsPerBeat(),
                                                   currentSampleRate);

    if (sps > 0.0)
        playheadSamples.store ((juce::int64) (clamped * sps));
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

namespace
{

/** Writes one automated value into an effect's parameters. Only the fields a
    type actually reads are automatable, so an unmatched code is a no-op rather
    than a silent write to the wrong parameter.
*/
void applyToEffect (EffectParams& params, AutomationParam param, float value) noexcept
{
    switch (param)
    {
        case AutomationParam::cutoff:     params.cutoff = value; break;
        case AutomationParam::resonance:  params.resonance = value; break;
        case AutomationParam::mix:        params.mix = value; break;
        case AutomationParam::roomSize:   params.roomSize = value; break;
        case AutomationParam::damping:    params.damping = value; break;
        case AutomationParam::width:      params.width = value; break;
        case AutomationParam::delayMs:    params.delayMs = value; break;
        case AutomationParam::feedback:   params.feedback = value; break;
        case AutomationParam::drive:      params.drive = value; break;
        case AutomationParam::outputGain: params.outputGain = value; break;
        case AutomationParam::rate:       params.rate = value; break;
        case AutomationParam::depth:      params.depth = value; break;
        case AutomationParam::lowGainDb:  params.lowGainDb = value; break;
        case AutomationParam::midGainDb:  params.midGainDb = value; break;
        case AutomationParam::midFreq:    params.midFreq = value; break;
        case AutomationParam::highGainDb: params.highGainDb = value; break;

        case AutomationParam::none:
        case AutomationParam::volume:
        case AutomationParam::pan:
        case AutomationParam::gain:
            break;
    }
}

} // namespace

void AudioEngine::collectAutomation (const EngineSnapshot& snapshot, double positionSteps) noexcept
{
    activeAutomation.clear();

    if (! snapshot.anyAutomation)
        return;

    const auto stepsPerBar = (double) snapshot.stepsPerBar();

    for (const auto& clip : snapshot.clips)
    {
        if (clip.automationIndex < 0 || ! clip.trackAudible
            || clip.automationIndex >= (int) snapshot.automations.size())
            continue;

        const auto start = (double) clip.startBar * stepsPerBar;
        const auto end = start + (double) clip.lengthBars * stepsPerBar;

        if (positionSteps < start || positionSteps >= end)
            continue;

        const auto& automation = snapshot.automations[(size_t) clip.automationIndex];

        if (automation.param == AutomationParam::none)
            continue;

        // The curve is drawn relative to the clip, so it plays wherever the
        // clip is placed rather than only at bar one.
        activeAutomation.push_back ({ automation.scope, automation.targetIndex,
                                      automation.slotIndex, automation.param,
                                      automation.valueAt (positionSteps - start) });
    }
}

void AudioEngine::applyAutomation (ChannelSnapshot& channel, int channelIndex) const noexcept
{
    for (const auto& active : activeAutomation)
    {
        if (active.targetIndex != channelIndex)
            continue;

        if (active.scope == AutomationScope::channel)
        {
            if (active.param == AutomationParam::volume)
                channel.volume = active.value;
            else if (active.param == AutomationParam::pan)
                channel.pan = active.value;
        }
        else if (active.scope == AutomationScope::channelEffect
                 && active.slotIndex >= 0 && active.slotIndex < channel.effects.numSlots)
        {
            applyToEffect (channel.effects.slots[(size_t) active.slotIndex].params,
                           active.param, active.value);
        }
    }
}

void AudioEngine::applyAutomation (MixerTrackSnapshot& track, int trackIndex) const noexcept
{
    for (const auto& active : activeAutomation)
    {
        if (active.targetIndex != trackIndex)
            continue;

        if (active.scope == AutomationScope::mixerTrack)
        {
            if (active.param == AutomationParam::gain)
                track.gain = active.value;
            else if (active.param == AutomationParam::pan)
                track.pan = active.value;
        }
        else if (active.scope == AutomationScope::mixerEffect
                 && active.slotIndex >= 0 && active.slotIndex < track.effects.numSlots)
        {
            applyToEffect (track.effects.slots[(size_t) active.slotIndex].params,
                           active.param, active.value);
        }
    }
}

float AudioEngine::automatedMasterGain (float base) const noexcept
{
    for (const auto& active : activeAutomation)
        if (active.scope == AutomationScope::master && active.param == AutomationParam::gain)
            return active.value;

    return base;
}

void AudioEngine::runChain (const EffectChainSnapshot& chain, float* left, float* right,
                            int numSamples) noexcept
{
    for (int i = 0; i < chain.numSlots; ++i)
    {
        const auto& slot = chain.slots[(size_t) i];

        if (! slot.enabled || slot.unitIndex < 0 || slot.unitIndex >= (int) effectUnits.size())
            continue;

        // A unit reused as a different effect must start clean: a reverb tail
        // read out through a delay line is noise, not a crossfade.
        const auto typeCode = (int) slot.type;

        if (effectUnitTypes[(size_t) slot.unitIndex] != typeCode)
        {
            effectUnitTypes[(size_t) slot.unitIndex] = typeCode;
            effectUnits[(size_t) slot.unitIndex]->reset();
        }

        effectUnits[(size_t) slot.unitIndex]->process (slot.type, slot.params, left, right, numSamples);
    }
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

    if (seekRequested.exchange (false))
    {
        const auto sps = Transport::samplesPerStepFor (transport.getTempo(),
                                                       transport.getStepsPerBeat(),
                                                       currentSampleRate);

        transport.setPositionSamples ((juce::int64) (seekToSteps.load() * sps));

        // Same reason as rewind below: jumping leaves anything that was
        // sounding with no note-off ahead of it.
        for (auto& channel : channels)
            channel.reset();
    }

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

    // Automation is a property of the arrangement, so it only applies in song
    // mode - pattern mode has no playlist position for a clip to cover.
    if (mode == Transport::Mode::song && isPlayingNow)
        collectAutomation (snapshot, transport.getPositionSamples() / juce::jmax (1.0, transport.samplesPerStep()));
    else
        activeAutomation.clear();

    const auto numChannels = juce::jmin ((int) snapshot.channels.size(), kMaxChannels);
    const auto numMixerTracks = juce::jmin ((int) snapshot.mixerTracks.size(), kMaxMixerTracks);

    channelBuffers.clear (0, numSamples);
    mixerBuffers.clear (0, numSamples);

    // Preview notes are drained whether or not the transport is running: the
    // whole point is to hear a pitch without playing the project.
    drainPreviewQueue (snapshot);

    // --- trigger notes -------------------------------------------------------
    for (const auto& trigger : triggers)
    {
        if (trigger.channelIndex < 0 || trigger.channelIndex >= numChannels)
            continue;

        const auto& channelSnapshot = snapshot.channels[(size_t) trigger.channelIndex];

        if (! snapshot.isChannelAudible (channelSnapshot))
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

        // One read of each controller per block, like the transport's atomics.
        channels[(size_t) i].renderAdd (mono, numSamples,
                                        channelBend[(size_t) i].load (std::memory_order_relaxed),
                                        channelModulation[(size_t) i].load (std::memory_order_relaxed));

        auto channelSnapshot = snapshot.channels[(size_t) i];

        if (! snapshot.isChannelAudible (channelSnapshot))
            continue;

        applyAutomation (channelSnapshot, i);

        const auto mixerIndex = channelSnapshot.mixerTrackIndex;

        if (mixerIndex < 0 || mixerIndex >= numMixerTracks)
            continue;

        auto* trackLeft  = mixerBuffers.getWritePointer (mixerIndex * 2);
        auto* trackRight = mixerBuffers.getWritePointer (mixerIndex * 2 + 1);

        if (channelSnapshot.effects.numSlots == 0)
        {
            MixerBus::addPanned (mono, numSamples, channelSnapshot.volume, channelSnapshot.pan,
                                 trackLeft, trackRight);
            continue;
        }

        // Effects are stereo, so a channel with a chain gets panned into a
        // scratch pair first and summed into its track afterwards.
        auto* scratchLeft  = channelStereo.getWritePointer (0);
        auto* scratchRight = channelStereo.getWritePointer (1);

        juce::FloatVectorOperations::clear (scratchLeft, numSamples);
        juce::FloatVectorOperations::clear (scratchRight, numSamples);

        MixerBus::addPanned (mono, numSamples, channelSnapshot.volume, channelSnapshot.pan,
                             scratchLeft, scratchRight);

        runChain (channelSnapshot.effects, scratchLeft, scratchRight, numSamples);

        juce::FloatVectorOperations::add (trackLeft, scratchLeft, numSamples);
        juce::FloatVectorOperations::add (trackRight, scratchRight, numSamples);
    }

    // --- mixer tracks into the master ----------------------------------------
    auto* outLeft = buffer.getWritePointer (0);
    auto* outRight = buffer.getWritePointer (1);

    for (int i = 0; i < numMixerTracks; ++i)
    {
        auto track = snapshot.mixerTracks[(size_t) i];

        if (! MixerBus::isAudible (snapshot, track))
            continue;

        applyAutomation (track, i);

        // Before gain and pan, so a track's fader rides the processed signal
        // rather than the effects riding the fader.
        runChain (track.effects, mixerBuffers.getWritePointer (i * 2),
                  mixerBuffers.getWritePointer (i * 2 + 1), numSamples);

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

        // The buffers hold the track PRE-fader - the gain is applied during the
        // add above - so the meter has to scale by what the fader is doing, or
        // it would sit beside a fader it does not answer to.
        recordPeak (trackPeaks[(size_t) i], mixerBuffers.getReadPointer (i * 2),
                    mixerBuffers.getReadPointer (i * 2 + 1), numSamples,
                    track.gain * juce::jmax (leftGain, rightGain) * juce::MathConstants<float>::sqrt2);
    }

    // On the summed mix, before the master fader - so the fader rides the
    // processed signal rather than the effects riding the fader.
    runChain (snapshot.masterEffects, outLeft, outRight, numSamples);

    buffer.applyGain (automatedMasterGain (snapshot.masterGain));

    recordPeak (masterPeak, outLeft, outRight, numSamples, 1.0f);

    // The same samples the master meter sees, and for the same reason: this is
    // the only point in the engine that is the finished output.
    signalTap.write (outLeft, outRight, numSamples);

    if (isPlayingNow && loopSteps > 0)
        transport.advance (numSamples);

    playheadSamples.store (transport.getPositionSamples());
}

} // namespace dew
