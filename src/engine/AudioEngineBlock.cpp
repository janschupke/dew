// =============================================================================
// The stages of one block.
//
// The same class, a second translation unit, and the one file in the engine
// that runs entirely on the audio thread.
//
// processBlock is the whole of it in order: settle the loop window, apply
// whatever the message thread asked for, collect the events this block
// contains, render the channels into their mixer tracks, sum those into the
// master. Each stage is a method rather than a comment inside one long
// function, and the header lists them under that heading.
//
// Two rules hold across all of it, and both are gated. Nothing here allocates,
// takes a lock, or throws. And nothing takes a snapshot entry BY VALUE - a
// ChannelSnapshot copied into an `auto` is a heap copy of its vectors on the
// audio thread, which a gate in the suite scans this file for by name.
// =============================================================================

#include "engine/AudioEngine.h"

#include "engine/SamplePlayer.h"

#include <cmath>
#include <limits>

namespace dew
{

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

    // AFTER the seek and the rewind, because panic() asks for a rewind too and
    // this has to be the last word on what is sounding. Everything else here
    // resets the instruments; this also silences the effect modules, which is
    // the difference between "no new sound" and "no sound" - a two second
    // reverb outlives every voice that fed it.
    if (panicRequested.exchange (false))
    {
        // The queues first, or a note-on already sitting in a ring would be
        // applied further down this very block and start a voice the panic was
        // meant to stop. Dropped rather than applied: what was queued is what
        // the panic is cancelling.
        previewQueue.discardPending();
        midiQueue.discardPending();

        resetAllInstruments();
        modulePool.resetAll();
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
        // Two pointers, and deliberately not named for one channel:
        // getWritePointer (i) still compiles after the resize and would
        // quietly hand back another channel's left side.
        auto* sourceLeft = channelBuffers.getWritePointer (i * 2);
        auto* sourceRight = channelBuffers.getWritePointer (i * 2 + 1);

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
        const auto& amp = automated != nullptr ? automated->amp : channel.amp;
        const auto& soundFontSettings = automated != nullptr ? automated->soundFontSettings
                                                             : channel.soundFontSettings;
        const auto& chain = automated != nullptr ? automated->effects : channel.effects;

        // One dispatch, not a branch on the kind of channel. It was an `if` on
        // an enum, with the two arms taking different arguments and sharing
        // nothing - which is what made a third kind of instrument a fourth
        // place to edit rather than a new class.
        auto* instrument = instrumentFor (i, channel.source);

        if (instrument != nullptr)
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
            blockContext.amp = &amp;
            blockContext.sample = &channel.sample;
            blockContext.audio = channel.audio.get();
            blockContext.soundFont = channel.soundFont.get();
            blockContext.soundFontSettings = &soundFontSettings;
            blockContext.channelIndex = i;

            instrument->processAdd (blockContext, { sourceLeft, sourceRight, numSamples });
        }

        // After the render, not before it: a note that starts in this block is
        // sounding by the time anybody can hear it, and one that ended in it is
        // not. Published for every channel whether or not it has an instrument,
        // so a channel that has just lost one stops reporting lit keys.
        publishSoundingPitches (i, instrument != nullptr ? instrument->soundingPitches()
                                                         : NoteMask {});

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
            MixerBus::addPanned (sourceLeft, sourceRight, numSamples, volume, pan, trackLeft,
                                 trackRight);
            continue;
        }

        // Effects are stereo, so a channel with a chain gets panned into a
        // scratch pair first and summed into its track afterwards.
        auto* scratchLeft = channelStereo.getWritePointer (0);
        auto* scratchRight = channelStereo.getWritePointer (1);

        juce::FloatVectorOperations::clear (scratchLeft, numSamples);
        juce::FloatVectorOperations::clear (scratchRight, numSamples);

        MixerBus::addPanned (sourceLeft, sourceRight, numSamples, volume, pan, scratchLeft,
                             scratchRight);

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

void AudioEngine::renderMetronome (const EngineSnapshot& snapshot, int numSamples,
                                   bool isPlayingNow, bool countingIn, int materialSteps,
                                   float* outLeft, float* outRight) noexcept
{
    if (! metronomeEnabled.load (std::memory_order_relaxed) && ! countingIn)
    {
        // Not a fade: the click is either wanted or it is not, and a tail at
        // this level is inaudible beside the discontinuity it would avoid.
        metronome.reset();
        return;
    }

    const auto stepsPerBeat = juce::jmax (1, transport.getStepsPerBeat());
    const auto beatsPerBar = juce::jmax (1, snapshot.beatsPerBar);
    const auto stepsPerBar = juce::jmax (stepsPerBeat, snapshot.stepsPerBar());

    // The block is rendered in SEGMENTS around the strikes rather than through
    // a queue of pending ones: at a long block size and a fast tempo several
    // beats can land in one buffer, and this has no bound to exceed and no drop
    // policy to get wrong.
    auto cursor = 0;

    const auto strikeAt = [&] (int offset, bool accent)
    {
        const auto at = juce::jlimit (cursor, numSamples, offset);

        metronome.render (outLeft + cursor, outRight + cursor, at - cursor);
        cursor = at;
        metronome.strike (accent);
    };

    if (countingIn)
    {
        // Anchored to the DOWNBEAT, not to where the count-in started. The
        // budget is spent a whole block at a time, so counting forwards would
        // put the join between the last click and the first bar out by up to a
        // block; counting back from zero puts that rounding in the lead-in,
        // where there is nothing to be out of time with.
        const auto samplesPerBeat = Transport::samplesPerStepFor (transport.getTempo(),
                                                                  stepsPerBeat, currentSampleRate)
                                    * (double) stepsPerBeat;

        if (samplesPerBeat <= 0.0)
            return;

        const auto remaining = (double) countInRemaining.load (std::memory_order_relaxed);

        // 0 <= remaining - k * samplesPerBeat < numSamples. Ascending offset is
        // DESCENDING k, which is the order the segmented render needs. k == 0
        // is the downbeat itself and belongs to the first PLAYING block.
        const auto last = (int) std::floor (remaining / samplesPerBeat);
        const auto first = (int) std::floor ((remaining - (double) numSamples) / samplesPerBeat)
                           + 1;

        for (auto k = last; k >= juce::jmax (1, first); --k)
            strikeAt ((int) std::llround (remaining - (double) k * samplesPerBeat),
                      k % beatsPerBar == 0);
    }
    else if (isPlayingNow && materialSteps > 0 && snapshot.tempoMap != nullptr)
    {
        // The same scan Sequencer::collect makes, over beats rather than steps,
        // and through the MAP for the reason it gives: a scalar rate drifts
        // against a ramp, and the drift accumulates against the sample counter.
        const auto& map = *snapshot.tempoMap;
        const auto blockStart = (double) transport.getPositionSamples();
        const auto blockEnd = blockStart + (double) numSamples;

        auto step = (juce::int64) std::ceil (map.stepsForSeconds (blockStart / currentSampleRate));
        const auto lastStep = (juce::int64) std::ceil (
                                  map.stepsForSeconds (blockEnd / currentSampleRate))
                              - 1;

        step = juce::jmax ((juce::int64) 0, step);

        for (; step <= lastStep; ++step)
        {
            if (step % stepsPerBeat != 0)
                continue;

            const auto at = map.secondsForSteps ((double) step) * currentSampleRate;

            if (at < blockStart || at >= blockEnd)
                continue;

            strikeAt ((int) std::llround (at - blockStart), step % stepsPerBar == 0);
        }
    }

    metronome.render (outLeft + cursor, outRight + cursor, numSamples - cursor);
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

    // ONE line is the whole count-in. isPlayingNow already gates the sequencer,
    // the automation, transport.advance AND blockContext.transport.isPlaying -
    // that last one matters most: a frozen transport that still says it is
    // playing makes SamplePlayer re-render the same window of the same sample
    // every block, which is a buzz for the length of the count-in.
    const auto countingIn = countInRemaining.load() > 0;
    const auto isPlayingNow = playing.load() && ! countingIn;

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

    // AFTER the fader, the meter and the tap, and BEFORE the advance so it
    // reads the same position the sequencer did. See renderMetronome.
    renderMetronome (snapshot, numSamples, isPlayingNow, countingIn, materialSteps, outLeft,
                     outRight);

    if (countingIn)
    {
        // A whole block at a time, and AudioRecorder's pre-roll counts down by
        // the same numSamples in the same device callback - so the two cannot
        // disagree about which block the take starts on.
        countInRemaining.store (
            juce::jmax ((juce::int64) 0, countInRemaining.load() - (juce::int64) numSamples));
    }

    if (isPlayingNow && materialSteps > 0)
        transport.advance (numSamples);

    playheadSamples.store (transport.getPositionSamples());
}

} // namespace dew
