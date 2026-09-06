#pragma once

#include <array>
#include <functional>

#include <juce_data_structures/juce_data_structures.h>

#include "engine/EngineSnapshot.h"

namespace dew::snapshotRead
{

/** Turning one node of the document into one piece of the engine's snapshot.

    These were file-local helpers at the top of EngineSnapshot.cpp, above a
    buildSnapshot that is 400 lines on its own. They are pure ValueTree -> POD
    converters: each takes a node and answers a value, and none of them touches
    the snapshot being built or knows what order the passes run in.

    A named namespace rather than a nested class, which is the convention the
    engine already uses for free functions. `warn` is a callback rather than a
    StringArray* because a reader reports what it could not honour and has no
    opinion about where that goes.
*/

/** A property read from a node and clamped by what the catalog declares it to
    be, so a hand-edited file cannot put the engine outside its own range. */
float clampBySpec (const juce::Identifier& property, const juce::ValueTree& node);

OscBankSnapshot readOscBank (const juce::ValueTree& instrument, const juce::String& ownerName,
                             const std::function<void (const juce::String&)>& warn);

AmpSettings readAmp (const juce::ValueTree& amp);

/** Which fixed effect unit an effect id owns this snapshot, or -1 when they are
    all spoken for. The `owners` array is the state, passed in rather than held,
    so the caller decides when a generation starts. */
int claimEffectUnit (int effectId, std::array<int, kMaxEffectUnits>& owners);

/** Which engine parameter a stored `param` names, within a scope.

    The SCOPE is taken because one spelling names two different parameters:
    `enabled` is an effect slot's bypass and an oscillator slot's on/off, on
    different nodes with different meanings. Every other repeated spelling in
    the catalog - `transpose` on a SAMPLE and a SOUNDFONT, `attack` on an AMP
    against the compressor's `attackMs` - is either between an automatable
    parameter and an unautomatable one or is two distinct identifiers, so the
    table answers those on its own.
*/
AutomationParam automationParamFromIdentifier (AutomationScope scope,
                                               const juce::Identifier& property);

EffectParamBlock readEffectParams (const juce::ValueTree& effect, EffectType type);

EffectChainSnapshot readEffectChain (const juce::ValueTree& owner, const juce::String& ownerName,
                                     std::array<int, kMaxEffectUnits>& unitOwners,
                                     const std::function<void (const juce::String&)>& warn);

void readSoundFont (ChannelSnapshot& c, const juce::ValueTree& channel,
                    SoundFontProvider* soundFonts,
                    const std::function<void (const juce::String&)>& warn);

void readSample (ChannelSnapshot& c, const juce::ValueTree& channel, SampleProvider* samples,
                 const std::function<void (const juce::String&)>& warn);

} // namespace dew::snapshotRead
