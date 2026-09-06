// =============================================================================
// AudioEngine's automation pass, and the overrides it produces.
//
// The same class, a second translation unit.
//
// Once a block, before anything is rendered, every automation lane in the
// snapshot is evaluated at the playhead and the results are written into two
// small override tables - one for channels, one for mixer tracks - plus the
// master gain. Everything downstream then reads a plain float and knows
// nothing about curves at all.
//
// The tables are members and are REUSED between blocks rather than rebuilt:
// this runs on the audio thread, where an allocation is a dropout. A gate in
// the suite asserts the render path allocates nothing, and it scans this file
// like any other.
// =============================================================================

#include "engine/AudioEngine.h"

#include <cmath>

#include "engine/ModuleFactory.h"
#include "engine/SnapshotReaders.h"

namespace dew
{

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
                || active.scope == AutomationScope::channelAmp
                || active.scope == AutomationScope::channelSoundFont
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
    overrides.amp = channel.amp;
    overrides.soundFontSettings = channel.soundFontSettings;
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
            auto& slot = overrides.osc.slots[(size_t) active.slotIndex];

            // All five the catalog declares, not only the position. The other
            // four reached the snapshot and stopped here, so a curve drawn over
            // an oscillator's gain or its unison spread moved a line on screen
            // and nothing in the sound.
            if (active.param == AutomationParam::position)
                slot.position = active.value;
            else if (active.param == AutomationParam::positionMod)
                slot.positionMod = active.value;
            else if (active.param == AutomationParam::positionRate)
                slot.positionRate = active.value;
            else if (active.param == AutomationParam::unisonDetune)
                slot.unisonDetune = active.value;
            else if (active.param == AutomationParam::gain)
                slot.gain = active.value;
            else if (active.param == AutomationParam::oscOctave)
                // The octave is an int in the snapshot and the curve is a
                // float, but automationValueFor has already snapped a discrete
                // parameter onto a whole value, so this truncates nothing a
                // curve meant.
                slot.octave = (int) active.value;
            else if (active.param == AutomationParam::oscDetuneCents)
                slot.detuneCents = active.value;
            else if (active.param == AutomationParam::oscEnabled)
                slot.enabled = active.value > 0.5f;
            else if (active.param == AutomationParam::lfoToPitch)
                slot.lfoToPitch = active.value;
            else if (active.param == AutomationParam::lfoToVolume)
                slot.lfoToVolume = active.value;
            else if (active.param == AutomationParam::lfoToPan)
                slot.lfoToPan = active.value;
            else if (active.param == AutomationParam::lfoRate && ! slot.lfoSync)
                // Only while the slot is free-running. A curve over the rate of
                // an LFO somebody locked to the tempo has no honest meaning:
                // applying it would silently unsync it, and the division would
                // still be what the panel showed.
                slot.lfoHz = active.value;

            else if (active.param == AutomationParam::fmTo1)
                slot.fmTo[0] = active.value;
            else if (active.param == AutomationParam::fmTo2)
                slot.fmTo[1] = active.value;
            else if (active.param == AutomationParam::fmTo3)
                slot.fmTo[2] = active.value;
            else if (active.param == AutomationParam::fmOut)
                slot.fmOut = active.value;

            // Whether the LFO is MOVING follows from the depths, and three of
            // them can have just changed - so it is recomputed here rather than
            // left as what the reader decided. Without this a curve that lifts
            // a depth off zero would raise a line on screen and nothing else,
            // because the voice puts only active slots in the LFO pass.
            slot.lfoActive = slot.lfoOn
                             && ! (juce::exactlyEqual (slot.lfoToPitch, 0.0f)
                                   && juce::exactlyEqual (slot.lfoToVolume, 0.0f)
                                   && juce::exactlyEqual (slot.lfoToPan, 0.0f));

            // Whether the MATRIX is asking for anything follows from the four
            // cells above in the same way, and for the same reason: a voice
            // that latched an empty matrix runs the plain path, so a curve
            // lifting an amount off zero would move a line and nothing else.
            //
            // Recomputed over the whole bank rather than for this slot alone,
            // because the flag is the BANK's - one slot routing into another is
            // a fact about the pair. It is a nine-cell scan on a block that
            // actually carries an FM curve, and nothing at all otherwise.
            overrides.osc.anyFm = snapshotRead::anyFmIn (overrides.osc);
        }
        else if (active.scope == AutomationScope::channelAmp)
        {
            // A voice latches its envelope at note-on, so writing these into
            // the bank the next note-on reads is the whole of the work - the
            // same shape an automated oscillator gain has always had.
            if (active.param == AutomationParam::ampAttack)
                overrides.amp.attack = active.value;
            else if (active.param == AutomationParam::ampDecay)
                overrides.amp.decay = active.value;
            else if (active.param == AutomationParam::ampSustain)
                overrides.amp.sustain = active.value;
            else if (active.param == AutomationParam::ampRelease)
                overrides.amp.release = active.value;
        }
        else if (active.scope == AutomationScope::channelSoundFont)
        {
            auto& sf = overrides.soundFontSettings;

            if (active.param == AutomationParam::sfTranspose)
                sf.transposeSemitones = active.value;
            else if (active.param == AutomationParam::sfTuneCents)
                sf.tuneCents = active.value;
            else if (active.param == AutomationParam::sfFilterOffset)
                sf.filterOffsetCents = active.value;
            else if (active.param == AutomationParam::sfAttackScale)
                sf.attackScale = active.value;
            else if (active.param == AutomationParam::sfReleaseScale)
                sf.releaseScale = active.value;
            else if (active.param == AutomationParam::sfVelocitySens)
                sf.velocitySensitivity = active.value;
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

} // namespace dew
