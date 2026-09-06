#pragma once

#include "engine/EngineSnapshot.h"

namespace dew
{

/** One automation curve, evaluated for this block.

    Collected once per block by AudioEngine::collectAutomation, which is what
    keeps the render path free of searching.
*/
struct ActiveAutomation
{
    AutomationScope scope = AutomationScope::channel;
    int targetIndex = -1;
    int slotIndex = -1;
    AutomationParam param = AutomationParam::none;
    int paramIndex = -1; ///< into the effect slot's block; -1 for other scopes
    float value = 0.0f;
};

/** A channel as this block plays it, rather than as the document declares it.

    Trivially copyable, and enforced as such where the engine holds it: the
    render path copies one of these every block, and a member that owned
    anything would put a refcount back on the audio thread.
*/
struct ChannelOverrides
{
    float volume = 0.0f;
    float pan = 0.0f;

    /** Whether the channel plays, which is the one such state a channel has. A
        curve over it is a curve over a VALUE on this channel, which is what
        made it automatable while the solo beside it was not - solo being a
        relation between channels rather than a value on one. */
    bool muted = false;

    OscBankSnapshot osc;

    /** The amplitude envelope and a soundfont channel's offsets.

        Both are read at note-on rather than per sample, so a curve over one
        moves the NEXT note - the same thing an automated oscillator gain has
        always done, and the reason this is a plain struct copy here rather than
        anything the voices have to learn about.
    */
    AmpSettings amp;
    SoundFontSettings soundFontSettings;

    EffectChainSnapshot effects;
};

/** The same, for a mixer track. */
struct MixerTrackOverrides
{
    float gain = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    EffectChainSnapshot effects;
};

/** Writes one evaluated curve into the channel it addresses.

    THE index of which AutomationParam reaches which field. Every enumerator
    that a channel scope applies is named exactly once, here, and
    AutomationOverrideTests drives every one of them through this function and
    requires that something moved - because the failure this shape invites is
    silent. A parameter declared automatable in the catalog, offered by the
    picker and drawn as a curve, but with no line here, moves a line on screen
    and nothing in the sound. Four oscillator parameters were in exactly that
    state before this file existed.

    Effect-block parameters are deliberately NOT named: they travel through
    `paramIndex`, resolved on the message thread where the slot's type is known.
    That was the fix for a sixteen-case switch that had to grow per effect, and
    it is why the effect branches here are two lines rather than a third ladder.

    Free function rather than a member, and taking the override rather than the
    engine, so it can be driven without an audio device, a snapshot or a
    playhead. noexcept and allocation-free: this is the render path.
*/
void applyAutomation (ChannelOverrides& overrides, const ActiveAutomation& active) noexcept;

/** The same, for a mixer track. Three parameters and the effect block. */
void applyAutomation (MixerTrackOverrides& overrides, const ActiveAutomation& active) noexcept;

} // namespace dew
