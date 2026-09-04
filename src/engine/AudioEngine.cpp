#include "engine/AudioEngine.h"

#include "engine/SamplePlayer.h"

#include <cmath>
#include <limits>
#include "engine/AtomicPeak.h"
#include "engine/ModuleFactory.h"

namespace dew
{

static_assert (std::atomic<AudioEngine::LoopRegion>::is_always_lock_free,
               "The loop region is read from the audio thread; a lock here would be a "
               "priority inversion, and the whole point of the packed pair is that it "
               "is published in one word.");

namespace
{

/** Which slot a mode's loop lives in. A function rather than a cast, so adding a
    third mode fails to compile here instead of silently aliasing an existing one.
*/
constexpr size_t loopSlotFor (Transport::Mode mode) noexcept
{
    return mode == Transport::Mode::song ? 1u : 0u;
}

} // namespace

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

    for (auto& channel : instruments)
    {
        for (auto& module : channel.byType)
            if (module != nullptr)
                module->prepare (currentSampleRate, currentBlockSize);
    }

    // Everything the audio thread might need, allocated once.
    channelBuffers.setSize (kMaxChannels, currentBlockSize);
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

    channelBuffers.setSize (0, 0);
    mixerBuffers.setSize (0, 0);
    channelStereo.setSize (0, 0);

    signalTap.reset();
}

void AudioEngine::setProject (const juce::ValueTree& project, juce::StringArray* warnings)
{
    publish (buildSnapshot (project, warnings, samplePool));
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

void AudioEngine::applySnapshotIfChanged (const EngineSnapshot& snapshot) noexcept
{
    if (snapshot.generation == appliedGeneration)
        return;

    appliedGeneration = snapshot.generation;
    transport.setTempo (snapshot.tempoBpm, snapshot.stepsPerBeat);
}

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

        // At the bound rather than growing, for the reason pushNoteEvent gives:
        // this runs on the audio thread and the vector is reserved once.
        //
        // The reserve is kMaxAutomations, but this loop walks CLIPS, and clips
        // are not capped - several may carry the same curve. So the bound has to
        // be tested here rather than inferred from the automation count.
        if ((int) activeAutomation.size() >= kMaxAutomations)
            return;

        // The curve is drawn relative to the clip, so it plays wherever the
        // clip is placed rather than only at bar one.
        activeAutomation.push_back ({ automation.scope, automation.targetIndex,
                                      automation.slotIndex, automation.param, automation.paramIndex,
                                      automation.valueAt (positionSteps - start) });
    }
}

namespace
{

/** Writes an automated value into a slot's parameter block.

    This was a sixteen-case switch - one per parameter of every effect type -
    that had to grow for each new one, and that silently did nothing for a
    parameter nobody had added a case for. The index is resolved on the message
    thread, where the slot's type is known, so the audio thread does an array
    write.
*/
void writeEffectParam (EffectSnapshot& slot, int paramIndex, float value) noexcept
{
    if (paramIndex >= 0 && paramIndex < kMaxEffectParams)
        slot.params[(size_t) paramIndex] = value;
}

} // namespace

const AudioEngine::ChannelOverrides* AudioEngine::overridesFor (const ChannelSnapshot& channel,
                                                                int channelIndex) noexcept
{
    auto automated = false;

    for (const auto& active : activeAutomation)
        if (active.targetIndex == channelIndex
            && (active.scope == AutomationScope::channel
                || active.scope == AutomationScope::channelOsc
                || active.scope == AutomationScope::channelEffect))
        {
            automated = true;
            break;
        }

    if (! automated)
        return nullptr;

    auto& overrides = channelOverrides[(size_t) channelIndex];

    overrides.volume = channel.volume;
    overrides.pan = channel.pan;
    overrides.muted = channel.muted;
    overrides.osc = channel.osc;
    overrides.effects = channel.effects;

    for (const auto& active : activeAutomation)
    {
        if (active.targetIndex != channelIndex)
            continue;

        if (active.scope == AutomationScope::channel)
        {
            if (active.param == AutomationParam::volume)
                overrides.volume = active.value;
            else if (active.param == AutomationParam::pan)
                overrides.pan = active.value;
            else if (active.param == AutomationParam::muted)
                // > 0.5f rather than != 0.0f: automationValueFor has already
                // snapped this to exactly 0 or 1, and -Wfloat-equal is an error
                // under the CI preset.
                overrides.muted = active.value > 0.5f;
        }
        else if (active.scope == AutomationScope::channelOsc && active.slotIndex >= 0
                 && active.slotIndex < overrides.osc.numSlots)
        {
            if (active.param == AutomationParam::position)
                overrides.osc.slots[(size_t) active.slotIndex].position = active.value;
        }
        else if (active.scope == AutomationScope::channelEffect && active.slotIndex >= 0
                 && active.slotIndex < overrides.effects.numSlots)
        {
            if (active.param == AutomationParam::enabled)
                overrides.effects.slots[(size_t) active.slotIndex].enabled = active.value > 0.5f;
            else
                writeEffectParam (overrides.effects.slots[(size_t) active.slotIndex],
                                  active.paramIndex, active.value);
        }
    }

    return &overrides;
}

const AudioEngine::MixerTrackOverrides* AudioEngine::overridesFor (const MixerTrackSnapshot& track,
                                                                   int trackIndex) noexcept
{
    auto automated = false;

    for (const auto& active : activeAutomation)
        if (active.targetIndex == trackIndex
            && (active.scope == AutomationScope::mixerTrack
                || active.scope == AutomationScope::mixerEffect))
        {
            automated = true;
            break;
        }

    if (! automated)
        return nullptr;

    auto& overrides = trackOverrides[(size_t) trackIndex];

    overrides.gain = track.gain;
    overrides.pan = track.pan;
    overrides.mute = track.mute;
    overrides.effects = track.effects;

    for (const auto& active : activeAutomation)
    {
        if (active.targetIndex != trackIndex)
            continue;

        if (active.scope == AutomationScope::mixerTrack)
        {
            if (active.param == AutomationParam::gain)
                overrides.gain = active.value;
            else if (active.param == AutomationParam::pan)
                overrides.pan = active.value;
            else if (active.param == AutomationParam::muted)
                overrides.mute = active.value > 0.5f;
        }
        else if (active.scope == AutomationScope::mixerEffect && active.slotIndex >= 0
                 && active.slotIndex < overrides.effects.numSlots)
        {
            if (active.param == AutomationParam::enabled)
                overrides.effects.slots[(size_t) active.slotIndex].enabled = active.value > 0.5f;
            else
                writeEffectParam (overrides.effects.slots[(size_t) active.slotIndex],
                                  active.paramIndex, active.value);
        }
    }

    return &overrides;
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

        if (! slot.enabled || slot.module == nullptr)
            continue;

        // A unit reused as a different effect must start clean: a reverb tail
        // read out through a delay line is noise, not a crossfade. Belt and
        // braces now that each type has its own object - but switching a slot
        // away and back would otherwise resume the first one's tail.
        const auto typeCode = (int) slot.type;

        if (slot.unitIndex >= 0 && slot.unitIndex < (int) effectUnitTypes.size()
            && effectUnitTypes[(size_t) slot.unitIndex] != typeCode)
        {
            effectUnitTypes[(size_t) slot.unitIndex] = typeCode;
            slot.module->reset();
        }

        processEffectSlot (*slot.module, slot.params, slot.type, { left, right, numSamples },
                           dryScratch);
    }
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

/** The window this block wraps in, applied to the transport.

    Returns whether the user's loop changed since the last block, which
    applyTransportRequests needs and nothing else does.
*/
bool AudioEngine::applyLoopWindow (Transport::Mode mode, int materialSteps) noexcept
{
    // The material's own extent is the default window, exactly as before. A
    // user's loop replaces it, clamped to the material - a loop past the end of
    // a pattern is not a shorter pattern, it is nothing to play.
    const auto userLoop = loopRegions[loopSlotFor (mode)].load (std::memory_order_relaxed);
    const auto loopChanged = ! (userLoop == lastAppliedLoop);
    lastAppliedLoop = userLoop;

    auto wrapStart = 0.0;
    auto wrapEnd = (double) materialSteps;

    if (materialSteps > 0 && ! userLoop.isEmpty())
    {
        const auto clampedStart = juce::jlimit (0.0, (double) materialSteps,
                                                (double) userLoop.startSteps);
        const auto clampedEnd = juce::jlimit (0.0, (double) materialSteps,
                                              (double) userLoop.endSteps);

        if (clampedEnd > clampedStart)
        {
            wrapStart = clampedStart;
            wrapEnd = clampedEnd;
        }
    }

    // Re-applied every block, like the length it replaces: the material can be
    // shortened underneath a loop, and the clamp has to move with it.
    transport.setLoopRange (wrapStart, wrapEnd);

    // Published for setPlayheadSteps, which has to fold a click the same way
    // this block will and cannot work the window out for itself.
    appliedWrap.store ({ (float) wrapStart, (float) wrapEnd }, std::memory_order_relaxed);

    return loopChanged;
}

/** Seeks, rewinds and the one case where a loop should snap rather than fold.

    Order is load-bearing and the comments below say why.
*/
void AudioEngine::applyTransportRequests (bool loopChanged) noexcept
{
    if (seekRequested.exchange (false))
    {
        const auto sps = Transport::samplesPerStepFor (
            transport.getTempo(), transport.getStepsPerBeat(), currentSampleRate);

        transport.setPositionSamples ((juce::int64) (seekToSteps.load() * sps));

        // One wrap rule: a click that lands past the loop end folds the same way
        // a block that ran past it does, rather than being corrected a block
        // later at whatever phase that block happened to end on.
        transport.wrapIntoLoop();

        // Same reason as rewind below: jumping leaves anything that was
        // sounding with no note-off ahead of it.
        resetAllInstruments();
    }

    if (rewindRequested.exchange (false))
    {
        transport.rewind();

        resetAllInstruments();
    }

    // A loop drawn BEHIND the playhead is the one case where folding is wrong:
    // the modulo would drop the playhead at an arbitrary point inside a region
    // the user has only just drawn. Snapping to its start is what they meant.
    //
    // Only on the CHANGE, so a loop that has not moved does not re-snap every
    // block; and only when the playhead is actually past the end, so shrinking a
    // loop from the right does it at most once. After the seek and rewind
    // branches, because a ruler click and a rewind are newer, more explicit
    // intent - and neither can trip this test anyway: rewind lands at zero, at
    // or before any loop start, and a seek has already been folded inside.
    //
    // Runs whether or not the transport is playing. If it only fired while
    // playing, setting a loop behind a stopped playhead would leave the position
    // outside it, and the first playing block would fold it in at an arbitrary
    // phase - the very stumble this exists to remove.
    if (loopChanged && transport.hasLoop()
        && transport.getPositionSamples() >= transport.loopEndSamples())
    {
        transport.setPositionSamples (transport.loopStartSamples());

        // Same reason a seek resets them: what was sounding has no note-off
        // ahead of it any more.
        resetAllInstruments();
    }
}

/** Everything the instruments will read: notes starting in this block, the
    automation covering it, and the scratch buffers cleared to take them.
*/
void AudioEngine::collectBlockEvents (const EngineSnapshot& snapshot, Transport::Mode mode,
                                      int patternIndex, int materialSteps, bool isPlayingNow,
                                      int numSamples, int numChannels) noexcept
{
    // Even when stopped, voices keep rendering so a note released at the moment
    // of stopping finishes its tail instead of clicking off.
    if (isPlayingNow && materialSteps > 0)
    {
        Sequencer::collect (snapshot, mode, transport.getPositionSamples(), numSamples,
                            *snapshot.tempoMap, currentSampleRate, patternIndex, triggers);
    }
    else
    {
        triggers.clear();
    }

    // Automation is a property of the arrangement, so it only applies in song
    // mode - pattern mode has no playlist position for a clip to cover.
    if (mode == Transport::Mode::song && isPlayingNow)
        collectAutomation (snapshot, transport.getPositionSamples()
                                         / juce::jmax (1.0, transport.samplesPerStep()));
    else
        activeAutomation.clear();

    channelBuffers.clear (0, numSamples);
    mixerBuffers.clear (0, numSamples);

    // Last block's events are gone; the vectors keep their storage.
    for (int i = 0; i < numChannels && i < (int) channelEvents.size(); ++i)
        channelEvents[(size_t) i].clear();

    // Preview notes are drained whether or not the transport is running: the
    // whole point is to hear a pitch without playing the project. Drained
    // first, so a preview and a step landing in the same block reach the
    // instrument in the order they always did.
    drainPreviewQueue (snapshot);

    // --- trigger notes -------------------------------------------------------
    for (const auto& trigger : triggers)
    {
        if (trigger.channelIndex < 0 || trigger.channelIndex >= numChannels)
            continue;

        const auto& channelSnapshot = snapshot.channels[(size_t) trigger.channelIndex];

        if (! snapshot.isChannelAudible (channelSnapshot))
            continue;

        pushNoteEvent (trigger.channelIndex,
                       { NoteEvent::Kind::on, trigger.sampleOffset, trigger.pitch, trigger.velocity,
                         trigger.durationSamples });
    }
}

/** Each channel's instrument into its mixer track, through its effect chain. */
void AudioEngine::renderChannels (const EngineSnapshot& snapshot, int numChannels,
                                  int numMixerTracks, int numSamples, bool isPlayingNow,
                                  Transport::Mode mode) noexcept
{
    // --- render channels into their mixer tracks -----------------------------
    // Everything an instrument needs that does not vary by channel.
    InstrumentContext blockContext;
    blockContext.transport = {
        transport.getPositionInSteps(), transport.samplesPerStep(), currentSampleRate,
        transport.getPositionSamples(), snapshot.tempoMap.get(),    isPlayingNow,
        mode == Transport::Mode::song
    };
    blockContext.clips = { snapshot.clips.data(), snapshot.clips.size() };
    blockContext.stepsPerBar = snapshot.stepsPerBar();

    for (int i = 0; i < numChannels; ++i)
    {
        auto* mono = channelBuffers.getWritePointer (i);

        const auto& channel = snapshot.channels[(size_t) i];

        // Before the render, not after it. Volume, pan and the effect chain are
        // all consumed further down, so this used to sit below; a wavetable
        // position has to be in the bank the voices read THIS block, or an
        // automated sweep would lag a block behind everything else.
        //
        // Null unless something actually automates this channel, so the common
        // case reads the snapshot straight through and copies nothing at all.
        const auto* automated = overridesFor (channel, i);

        const auto volume = automated != nullptr ? automated->volume : channel.volume;
        const auto pan = automated != nullptr ? automated->pan : channel.pan;
        const auto& osc = automated != nullptr ? automated->osc : channel.osc;
        const auto& chain = automated != nullptr ? automated->effects : channel.effects;

        // One dispatch, not a branch on the kind of channel. It was an `if` on
        // an enum, with the two arms taking different arguments and sharing
        // nothing - which is what made a third kind of instrument a fourth
        // place to edit rather than a new class.
        if (auto* instrument = instrumentFor (i, channel.source))
        {
            // Filled in place, not copied. The block-wide half was set once
            // above the loop; only these change per channel. A copy here was
            // measurably slower - a hundred bytes per channel per block adds up
            // on a path that runs eighty-six times a second per channel.
            blockContext.events = { channelEvents[(size_t) i].data(),
                                    channelEvents[(size_t) i].size() };

            // One read of each controller per block, like the transport's atomics.
            blockContext.bendSemitones = channelBend[(size_t) i].load (std::memory_order_relaxed);
            blockContext.modulation = channelModulation[(size_t) i].load (
                std::memory_order_relaxed);

            blockContext.osc = &osc;
            blockContext.amp = &channel.amp;
            blockContext.sample = &channel.sample;
            blockContext.audio = channel.audio.get();
            blockContext.channelIndex = i;

            instrument->processAdd (blockContext, mono, numSamples);
        }

        // The override, not the snapshot: a curve over mute has to silence the
        // channel this block, and `automated` is already in hand.
        if (! snapshot.isChannelAudible (channel,
                                         automated != nullptr ? automated->muted : channel.muted))
            continue;

        const auto mixerIndex = channel.mixerTrackIndex;

        if (mixerIndex < 0 || mixerIndex >= numMixerTracks)
            continue;

        auto* trackLeft = mixerBuffers.getWritePointer (mixerIndex * 2);
        auto* trackRight = mixerBuffers.getWritePointer (mixerIndex * 2 + 1);

        // anyEnabled rather than numSlots: a chain whose slots are all switched
        // off used to take the whole stereo detour - clear a scratch pair, pan
        // into it, run a chain that does nothing, add it back - because it still
        // HAD slots. Bit-exact, since adding into a cleared buffer and then into
        // the track is the same arithmetic as adding into the track.
        if (! chain.anyEnabled())
        {
            MixerBus::addPanned (mono, numSamples, volume, pan, trackLeft, trackRight);
            continue;
        }

        // Effects are stereo, so a channel with a chain gets panned into a
        // scratch pair first and summed into its track afterwards.
        auto* scratchLeft = channelStereo.getWritePointer (0);
        auto* scratchRight = channelStereo.getWritePointer (1);

        juce::FloatVectorOperations::clear (scratchLeft, numSamples);
        juce::FloatVectorOperations::clear (scratchRight, numSamples);

        MixerBus::addPanned (mono, numSamples, volume, pan, scratchLeft, scratchRight);

        runChain (chain, scratchLeft, scratchRight, numSamples);

        juce::FloatVectorOperations::add (trackLeft, scratchLeft, numSamples);
        juce::FloatVectorOperations::add (trackRight, scratchRight, numSamples);
    }
}

/** The mixer tracks summed into the master pair, metered as they go. */
void AudioEngine::sumMixerTracks (const EngineSnapshot& snapshot, int numMixerTracks,
                                  int numSamples, float* outLeft, float* outRight) noexcept
{
    for (int i = 0; i < numMixerTracks; ++i)
    {
        const auto& track = snapshot.mixerTracks[(size_t) i];

        const auto* automated = overridesFor (track, i);

        if (! MixerBus::isAudible (snapshot, track,
                                   automated != nullptr ? automated->mute : track.mute))
            continue;

        const auto trackGain = automated != nullptr ? automated->gain : track.gain;
        const auto trackPan = automated != nullptr ? automated->pan : track.pan;
        const auto& chain = automated != nullptr ? automated->effects : track.effects;

        // Before gain and pan, so a track's fader rides the processed signal
        // rather than the effects riding the fader.
        runChain (chain, mixerBuffers.getWritePointer (i * 2),
                  mixerBuffers.getWritePointer (i * 2 + 1), numSamples);

        const auto gains = MixerBus::trackGains (trackPan, trackGain);

        juce::FloatVectorOperations::addWithMultiply (outLeft, mixerBuffers.getReadPointer (i * 2),
                                                      gains.left, numSamples);
        juce::FloatVectorOperations::addWithMultiply (
            outRight, mixerBuffers.getReadPointer (i * 2 + 1), gains.right, numSamples);

        // The buffers hold the track PRE-fader - the gain is applied during the
        // add above - so the meter has to scale by what the fader is doing, or
        // it would sit beside a fader it does not answer to.
        recordPeak (trackPeaks[(size_t) i], mixerBuffers.getReadPointer (i * 2),
                    mixerBuffers.getReadPointer (i * 2 + 1), numSamples, gains.meter);
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

    // Every block, not only when the generation changes: the bridge can hand
    // back a different slot holding the same generation, and a raw pointer into
    // the previous one would outlive it.
    transport.setTempoMap (snapshot.tempoMap.get());

    const auto mode = requestedMode.load();
    transport.setMode (mode);

    const auto patternIndex = snapshot.patternIndexForId (requestedPatternId.load());

    const auto materialSteps = Sequencer::materialLengthSteps (snapshot, mode, patternIndex);

    const auto numChannels = juce::jmin ((int) snapshot.channels.size(), kMaxChannels);
    const auto numMixerTracks = juce::jmin ((int) snapshot.mixerTracks.size(), kMaxMixerTracks);

    const auto loopChanged = applyLoopWindow (mode, materialSteps);

    applyTransportRequests (loopChanged);

    const auto isPlayingNow = playing.load();

    collectBlockEvents (snapshot, mode, patternIndex, materialSteps, isPlayingNow, numSamples,
                        numChannels);

    renderChannels (snapshot, numChannels, numMixerTracks, numSamples, isPlayingNow, mode);

    auto* outLeft = buffer.getWritePointer (0);
    auto* outRight = buffer.getWritePointer (1);

    sumMixerTracks (snapshot, numMixerTracks, numSamples, outLeft, outRight);

    // On the summed mix, before the master fader - so the fader rides the
    // processed signal rather than the effects riding the fader.
    runChain (snapshot.masterEffects, outLeft, outRight, numSamples);

    buffer.applyGain (automatedMasterGain (snapshot.masterGain));

    recordPeak (masterPeak, outLeft, outRight, numSamples, 1.0f);

    // The same samples the master meter sees, and for the same reason: this is
    // the only point in the engine that is the finished output.
    signalTap.write (outLeft, outRight, numSamples);

    if (isPlayingNow && materialSteps > 0)
        transport.advance (numSamples);

    playheadSamples.store (transport.getPositionSamples());
}

} // namespace dew
