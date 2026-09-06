#include "engine/AudioEngine.h"

#include <limits>

#include "engine/AtomicPeak.h"
#include "engine/ModuleFactory.h"

namespace dew
{

static_assert (std::atomic<AudioEngine::LoopRegion>::is_always_lock_free,
               "The loop region is read from the audio thread; a lock here would be a "
               "priority inversion, and the whole point of the packed pair is that it "
               "is published in one word.");

AudioEngine::AudioEngine()
{
    instruments.resize (kMaxChannels);
    channelEvents.resize (kMaxChannels);

    for (auto& events : channelEvents)
        events.reserve ((size_t) maxEventsPerChannel);
    triggers.reserve ((size_t) kMaxTriggersPerBlock);

    effectUnitTypes.assign (kMaxEffectUnits, -1);
    activeAutomation.reserve (kMaxAutomations);

    // Sized here rather than in prepare(), because the render path indexes them
    // by channel and track and must never find them short.
    channelOverrides.resize (kMaxChannels);
    trackOverrides.resize (kMaxMixerTracks);
}

void AudioEngine::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
    currentBlockSize = juce::jmax (1, maximumBlockSize);

    transport.prepare (currentSampleRate);
    signalTap.setSampleRate (currentSampleRate);
    metronome.prepare (currentSampleRate);

    for (auto& channel : instruments)
    {
        for (auto& module : channel.byType)
            if (module != nullptr)
                module->prepare (currentSampleRate, currentBlockSize);
    }

    // Everything the audio thread might need, allocated once.
    // Two per channel: an instrument writes a stereo pair, and the pair is what
    // the mixer pans. Interleaved by channel, the way mixerBuffers already is.
    channelBuffers.setSize (kMaxChannels * 2, currentBlockSize);
    mixerBuffers.setSize (kMaxMixerTracks * 2, currentBlockSize);
    channelStereo.setSize (2, currentBlockSize);
    channelBuffers.clear();
    mixerBuffers.clear();
    channelStereo.clear();

    modulePool.prepare (currentSampleRate, currentBlockSize);
    dryScratch.setSize (2, currentBlockSize);
    dryScratch.clear();

    effectUnitTypes.assign (kMaxEffectUnits, -1);

    triggers.reserve ((size_t) kMaxTriggersPerBlock);
}

void AudioEngine::releaseResources()
{
    resetAllInstruments();
    metronome.reset();

    channelBuffers.setSize (0, 0);
    mixerBuffers.setSize (0, 0);
    channelStereo.setSize (0, 0);

    signalTap.reset();
}

void AudioEngine::setProject (const juce::ValueTree& project, juce::StringArray* warnings)
{
    publish (buildSnapshot (project, warnings, samplePool, soundFontPool));
}

void AudioEngine::publish (EngineSnapshot snapshot)
{
    // Every snapshot goes through here, so this is the one place a slot can be
    // pointed at its DSP - and it is on the message thread, which is where
    // making a module is allowed to allocate.
    resolveModules (snapshot);

    // The message thread's own copy, taken before the snapshot is handed over.
    // It converts for the ruler and for a seek; the audio thread uses the one in
    // the snapshot it has latched.
    uiTempoMap = snapshot.tempoMap;

    bridge.publish (std::move (snapshot));
}

bool AudioEngine::previewNoteOn (int channelIndex, int pitch, float velocity) noexcept
{
    return previewQueue.push ({ PreviewEvent::Kind::noteOn, channelIndex,
                                juce::jlimit (0, 127, pitch),
                                juce::jlimit (0.0f, 1.0f, velocity) });
}

bool AudioEngine::previewNoteOff (int channelIndex, int pitch) noexcept
{
    return previewQueue.push (
        { PreviewEvent::Kind::noteOff, channelIndex, juce::jlimit (0, 127, pitch), 0.0f });
}

bool AudioEngine::previewAllOff() noexcept
{
    return previewQueue.push ({ PreviewEvent::Kind::allOff, 0, 0, 0.0f });
}

bool AudioEngine::midiNoteOn (int channelIndex, int pitch, float velocity) noexcept
{
    return midiQueue.push ({ PreviewEvent::Kind::noteOn, channelIndex, juce::jlimit (0, 127, pitch),
                             juce::jlimit (0.0f, 1.0f, velocity) });
}

bool AudioEngine::midiNoteOff (int channelIndex, int pitch) noexcept
{
    return midiQueue.push (
        { PreviewEvent::Kind::noteOff, channelIndex, juce::jlimit (0, 127, pitch), 0.0f });
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
        for (int i = 0; i < numChannels; ++i)
            pushNoteEvent (i, { NoteEvent::Kind::allOff });

        return;
    }

    if (event.channelIndex < 0 || event.channelIndex >= numChannels)
        return;

    if (event.kind == PreviewEvent::Kind::noteOff)
    {
        pushNoteEvent (event.channelIndex, { NoteEvent::Kind::off, 0, event.pitch });
        return;
    }

    // A preview note has no duration to run out - it lasts until the user
    // lets go - so it is triggered with one long enough that the release
    // always comes first.
    //
    // Deliberately NOT gated on isChannelAudible, though the sequencer's
    // triggers are. It looks like an inconsistency and was reported as one, but
    // the render loop already refuses to sum an inaudible channel into its
    // mixer track, so a preview on a muted channel is silent either way. Adding
    // the check here would save a voice nobody can hear and change one thing
    // that can be heard: unmuting mid-preview would no longer let the held note
    // through. Not worth an untestable behaviour change.
    juce::ignoreUnused (snapshot);
    pushNoteEvent (event.channelIndex, { NoteEvent::Kind::on, 0, event.pitch, event.velocity,
                                         std::numeric_limits<int>::max() });
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

    atomicPeakMax (slot, peak);
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
    // Cleared FIRST: pressing Space during a count-in means "play now", and a
    // budget left standing would swallow the first bar of it.
    countInRemaining.store (0);
    playing.store (true);
}

void AudioEngine::stop()
{
    playing.store (false);
    countInRemaining.store (0);
}

void AudioEngine::pause()
{
    playing.store (false);
    countInRemaining.store (0);
    setPlayheadSteps (startMarkerSteps.load());
}

void AudioEngine::setMetronomeEnabled (bool shouldClick) noexcept
{
    metronomeEnabled.store (shouldClick);
}

bool AudioEngine::isMetronomeEnabled() const noexcept
{
    return metronomeEnabled.load();
}

void AudioEngine::playWithCountIn (juce::int64 countInSamples) noexcept
{
    // The budget before the flag, for the reason rewind() gives about the start
    // marker: a processBlock that lands between the two must not see a playing
    // transport with no count-in and sequence the bar this call is counting in.
    countInRemaining.store (juce::jmax ((juce::int64) 0, countInSamples));
    playing.store (true);
}

bool AudioEngine::isCountingIn() const noexcept
{
    return countInRemaining.load() > 0;
}

juce::int64 AudioEngine::getCountInRemainingSamples() const noexcept
{
    return countInRemaining.load();
}

void AudioEngine::setStartMarkerSteps (double steps)
{
    const auto clamped = juce::jmax (0.0, steps);

    startMarkerSteps.store (clamped);
    setPlayheadSteps (clamped);
}

void AudioEngine::rewind()
{
    // The marker goes with it - see the header. Stored before the request, so a
    // processBlock that lands between the two cannot pause back to a marker
    // that this call has already decided is gone.
    startMarkerSteps.store (0.0);
    countInRemaining.store (0);

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

void AudioEngine::panic() noexcept
{
    // Everything the calling thread can do itself, so a panic is heard even
    // when no device is calling back - the same reason rewind() zeroes the
    // playhead here rather than waiting for a block that may never come.
    playing.store (false);
    resetControllers();
    rewind();

    // And the rest, which is the audio thread's: voices to kill and effect
    // modules to silence. Set LAST, so a block that runs between the lines
    // above and this one still sees the request.
    panicRequested.store (true);
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
                                                   transport.getStepsPerBeat(), currentSampleRate);

    if (sps <= 0.0)
        return;

    // Through the message thread's own map, so a seek under a tempo curve lands
    // where the ruler drew it. Both ends of the wrap below convert with the SAME
    // map, which is strictly better than the old arithmetic: that re-derived a
    // constant rate and could round differently from the audio thread.
    const auto map = uiTempoMap;

    const auto samplesAt = [&map, sps, this] (double atSteps)
    {
        if (map != nullptr && ! map->isConstant())
            return map->secondsForSteps (atSteps) * currentSampleRate;

        return sps * atSteps;
    };

    auto position = (juce::int64) samplesAt (clamped);

    // Folded HERE as well, through the one wrap rule, so what is shown at once
    // is where the next block will actually land. Storing the raw position and
    // letting the audio thread fold it a moment later is what made a click
    // outside the loop paint the clicked spot and then jump - the seeker
    // flickering. Rounded from the tempo the same way Transport rounds it, so
    // the two cannot land a sample apart.
    const auto wrap = appliedWrap.load (std::memory_order_relaxed);

    if (! wrap.isEmpty())
        position = Transport::wrappedIntoLoop (
            position, (juce::int64) std::llround (samplesAt ((double) wrap.startSteps)),
            (juce::int64) std::llround (samplesAt ((double) wrap.endSteps)));

    playheadSamples.store (position);
}

void AudioEngine::setLoopRangeSteps (Transport::Mode mode, double startSteps,
                                     double endSteps) noexcept
{
    // Ordered here rather than in Transport: which end of a drag came first is a
    // fact about a mouse, not about time.
    const auto lo = juce::jmax (0.0, juce::jmin (startSteps, endSteps));
    const auto hi = juce::jmax (0.0, juce::jmax (startSteps, endSteps));

    // Relaxed: the range carries no other data, so there is nothing for it to
    // publish a happens-before edge to - the same argument the controllers make.
    loopRegions[loopSlotFor (mode)].store ({ (float) lo, (float) hi }, std::memory_order_relaxed);
}

void AudioEngine::clearLoopRange (Transport::Mode mode) noexcept
{
    loopRegions[loopSlotFor (mode)].store ({}, std::memory_order_relaxed);
}

AudioEngine::LoopRegion AudioEngine::getLoopRegion (Transport::Mode mode) const noexcept
{
    return loopRegions[loopSlotFor (mode)].load (std::memory_order_relaxed);
}

bool AudioEngine::hasLoopRegion (Transport::Mode mode) const noexcept
{
    return ! getLoopRegion (mode).isEmpty();
}

void AudioEngine::setMode (Transport::Mode mode)
{
    requestedMode.store (mode);
}

double AudioEngine::getPlayheadSteps() const noexcept
{
    // Through the MESSAGE thread's own copy of the map, not the transport's:
    // the transport's pointer is into whatever snapshot the audio thread has
    // latched, and reading it from here would be a race.
    const auto map = uiTempoMap;

    if (map != nullptr && ! map->isConstant())
        return map->stepsForSeconds ((double) playheadSamples.load()
                                     / juce::jmax (1.0, currentSampleRate));

    const auto sps = Transport::samplesPerStepFor (transport.getTempo(),
                                                   transport.getStepsPerBeat(), currentSampleRate);

    return sps > 0.0 ? (double) playheadSamples.load() / sps : 0.0;
}

int AudioEngine::getMaterialisedInstrumentCount (InstrumentType type) const noexcept
{
    auto count = 0;

    for (const auto& channel : instruments)
        if (channel.byType[(size_t) type] != nullptr)
            ++count;

    return count;
}

void AudioEngine::pushNoteEvent (int channelIndex, const NoteEvent& event) noexcept
{
    if (channelIndex < 0 || channelIndex >= (int) channelEvents.size())
        return;

    auto& events = channelEvents[(size_t) channelIndex];

    // At the bound rather than growing: push_back on a full vector allocates,
    // and this runs on the audio thread.
    if ((int) events.size() >= maxEventsPerChannel)
        return;

    events.push_back (event);
}

InstrumentModule* AudioEngine::instrumentFor (int channelIndex, InstrumentType type) noexcept
{
    if (channelIndex < 0 || channelIndex >= (int) instruments.size())
        return nullptr;

    auto& channel = instruments[(size_t) channelIndex];

    // Never made here - only read. Making one allocates, and this runs on the
    // audio thread; resolveModules does the making, on the message thread. With
    // the array there is no longer anything here that COULD make one.
    return channel.byType[(size_t) type].get();
}

void AudioEngine::resetAllInstruments() noexcept
{
    for (auto& channel : instruments)
    {
        for (auto& module : channel.byType)
            if (module != nullptr)
                module->reset();
    }
}

void AudioEngine::resolveModules (EngineSnapshot& snapshot)
{
    // Instruments, made for what the project actually has. Sixty-four
    // SynthChannels - a thousand and twenty-four voices - used to exist
    // whether or not a single channel was a synth, so a project of audio
    // channels paid for sixteen voices each of nothing.
    for (size_t i = 0; i < snapshot.channels.size() && i < instruments.size(); ++i)
    {
        auto& channel = instruments[i];

        const auto source = snapshot.channels[i].source;
        auto& module = channel.byType[(size_t) source];

        if (module == nullptr)
        {
            module = createInstrumentModule (source);

            if (module != nullptr)
                module->prepare (currentSampleRate, currentBlockSize);
        }
    }

    // Message thread, between building a snapshot and publishing it: acquire()
    // allocates on first use, and the audio thread must never do that.
    const auto resolve = [this] (EffectChainSnapshot& chain)
    {
        for (int i = 0; i < chain.numSlots; ++i)
        {
            auto& slot = chain.slots[(size_t) i];
            slot.module = slot.unitIndex >= 0 ? modulePool.acquire (slot.unitIndex, slot.type)
                                              : nullptr;
        }
    };

    for (auto& channel : snapshot.channels)
        resolve (channel.effects);

    for (auto& track : snapshot.mixerTracks)
        resolve (track.effects);

    resolve (snapshot.masterEffects);
}

} // namespace dew
