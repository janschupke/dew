#include "engine/AutomationOverrides.h"

#include "engine/SnapshotReaders.h"

namespace dew
{

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

void applyAutomation (ChannelOverrides& overrides, const ActiveAutomation& active) noexcept
{
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
            writeEffectParam (overrides.effects.slots[(size_t) active.slotIndex], active.paramIndex,
                              active.value);
    }
}

void applyAutomation (MixerTrackOverrides& overrides, const ActiveAutomation& active) noexcept
{
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
            writeEffectParam (overrides.effects.slots[(size_t) active.slotIndex], active.paramIndex,
                              active.value);
    }
}

} // namespace dew
