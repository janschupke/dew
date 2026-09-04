#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

#include "model/ParamSpec.h"

namespace dew
{

/** What an automation clip can be pointed at.

    Deliberately a declared set rather than an arbitrary property path. A path
    would automate anything, including a channel's name or a pattern's length,
    and would silently break the moment a property was renamed. Every entry
    resolves to a ParamSpec that says what the parameter is, and the engine
    resolves the target to a direct index so the audio thread does no lookup.

    WHICH parameters are worth a curve is still a judgement - it is just made
    once, beside the parameter, as ParamSpec::automatable, rather than in a
    second list of names far away from it. Those lists had already drifted from
    what the catalog declared.
*/
enum class AutomationScope
{
    /** The arrangement itself: the tempo, and nothing else.

        Not the meter: a clip is stored in BARS, and ProjectEdits::setMeter
        rescales every clip's start and length to hold its position in steps -
        which a per-block curve cannot do. And not barsInSong, which is a
        document extent rather than a quantity. Saying so here is what stops the
        scope accreting.
    */
    project,

    channel,       ///< a channel's own volume or pan
    channelOsc,    ///< a parameter of one oscillator slot on a channel
    channelEffect, ///< a parameter of one slot in a channel's chain
    mixerTrack,    ///< a mixer track's gain or pan
    mixerEffect,   ///< a parameter of one slot in a mixer track's chain
    master         ///< the master gain
};

AutomationScope automationScopeFromString (const juce::String&);
juce::String automationScopeToString (AutomationScope);

/** Maps a 0..1 automation value onto a parameter's own units.

    NOT ParamSpec::fromNormalised, which is what a knob reads and is continuous
    on purpose. This one SNAPS a discrete parameter, because half-on is not a
    state a bool has and a filter mode between two modes is not a mode. Doing it
    here - in the one function the picker, the point editor, the painter and the
    engine all call - is what keeps "what you draw is what you hear" true for a
    toggle as well as for a cutoff.
*/
double automationValueFor (const ParamSpec&, double normalised);

/** Parameters automatable on the arrangement itself: the tempo. */
const std::vector<ParamSpec>& projectParams();

/** Parameters automatable on a channel itself. */
const std::vector<ParamSpec>& channelParams();

/** Parameters automatable on a mixer track itself. */
const std::vector<ParamSpec>& mixerTrackParams();

/** Parameters automatable on the master. */
const std::vector<ParamSpec>& masterParams();

/** Parameters automatable on one oscillator slot.

    Only offered for a slot in wavetable mode - see availableAutomationTargets.
    A classic oscillator's wave position means nothing, and offering it would be
    a control that silently did nothing.
*/
const std::vector<ParamSpec>& oscParams();

/** Parameters automatable on an effect of this type. */
const std::vector<ParamSpec>& effectParams (const juce::String& effectType);

/** Every automatable parameter's spelling, for the source gate.

    One call, so a table added beside the others cannot be forgotten by the gate
    that stops these names being written as string literals.
*/
juce::StringArray automatableParameterNames();

/** The spec for one property within a scope, or nullptr if it is not
    automatable - which is how a clip pointing at a parameter that no longer
    applies (an effect slot changed type) is dropped rather than misapplied.

    The returned pointer is into a table with static storage duration, so it is
    safe to hold: the engine's snapshot keeps one and reads it on the audio
    thread.
*/
const ParamSpec* findParamSpec (AutomationScope, const juce::String& effectType,
                                const juce::Identifier& property);

/** Every target a project currently offers, as the picker shows them. */
struct AutomationTarget
{
    AutomationScope scope = AutomationScope::channel;
    int targetId = 0; ///< channel id or mixer track id; 0 for master
    int slot = -1;    ///< effect or oscillator slot index, -1 when the scope has none
    juce::Identifier property;
    juce::String displayName; ///< "Kick > Filter > Cutoff"

    /** What the parameter IS, rather than four fields copied out of it.

        The range, the curve, whether it is bipolar and how many values it has
        used to be restated here and in three other tables, and they had drifted:
        a mixer fader offered 0..1.5, the engine clamped at 2.0 and automation
        mapped onto 0..1, so a curve drawn to the top reached two thirds of the
        travel and stopped with nothing saying why.
    */
    const ParamSpec* spec = nullptr;
};

/** The automation target a property on a node names, or nothing.

    The INVERSE of the picker, and deliberately the same function underneath:
    availableAutomationTargets is a WALK over this, so a target the picker offers
    and a target a control's right-click creates cannot be two different things.
    Before this, the owner-node -> (scope, targetId, slot) mapping existed only
    inside the picker's own loop, and a knob had nowhere to ask what it drove.

    `project` is taken because a node cannot always classify itself: an EFFECT
    under a CHANNEL is a channelEffect and the same node under a MIXER_TRACK is
    a mixerEffect, and its slot is its position among its EFFECT siblings.
*/
std::optional<AutomationTarget> automationTargetFor (const juce::ValueTree& project,
                                                     const juce::ValueTree& node,
                                                     const juce::Identifier& property);

std::vector<AutomationTarget> availableAutomationTargets (const juce::ValueTree& project);

/** What an AUTOMATION node drives, resolved from what it stores.

    The third direction, and the one that was missing. `automationTargetFor`
    goes node -> target for a control that knows which node it was built from;
    this goes the other way, for a painter that has only a clip and the four
    properties on it - scope, targetId, slot and param.

    The playlist has been going without it and saying so: its automation clip
    was drawn in one colour for every target and with `bipolar` hard-coded
    false, because "resolving its (scope, targetId, slot) back to a ParamSpec"
    had nowhere to happen. A pan curve therefore filled from the bottom instead
    of from the centre, which is a curve drawn unlike the knob it drives.

    Returns nullptr when the clip points at something that no longer applies -
    an effect slot that changed type, an oscillator switched out of wavetable
    mode - which is the same answer findParamSpec already gives, so a stale clip
    is drawn inert rather than misread.

    The pointer is into a table with static storage duration and is safe to
    hold.
*/
const ParamSpec* specForAutomation (const juce::ValueTree& project,
                                    const juce::ValueTree& automation);

/** The node an address names, or an invalid tree.

    The fourth direction, and the inverse of automationTargetFor: that one goes
    node -> (scope, targetId, slot); this one goes back. Both halves of one fact,
    so a caller that has only an address - an automation clip, or a control
    surface outside the interface - resolves it exactly where the picker does
    rather than re-deriving the tree walk.

    It existed already, as two file-local helpers specForAutomation used to find
    an effect's TYPE. Naming it is what stops the next caller writing a third
    copy of "a channel by id, then the nth EFFECT under it", which is the walk
    that has to agree with slotOf or an address silently means a different
    effect.

    `slot` is ignored by every scope that has none, so an address carrying a
    stale slot still resolves rather than failing for a field nothing reads.

    A test asserts the round trip over every target availableAutomationTargets
    offers, which is what holds the two directions together.
*/
juce::ValueTree automationNodeFor (const juce::ValueTree& project, AutomationScope scope,
                                   int targetId, int slot);

} // namespace dew
