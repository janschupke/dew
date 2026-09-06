#pragma once

#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "model/AutomationTargets.h"
#include "model/ParamSpec.h"

namespace dew::control
{

/** Where one parameter lives, in the terms the document already uses.

    Four fields cover every parameter in a project - a channel's volume, an
    oscillator's detune, an envelope's release, a sample's fade, a soundfont's
    velocity sensitivity, any effect's anything, a mixer fader, the master, and
    the tempo - because the fields are not invented here. `group` is the
    `jsonKey` of a ParamGroup in ModuleCatalog, which is the table that already
    says which NODE a run of parameters lives on; `slot` is the position among
    a repeated group's slots, which is what `count` in that same table means.

    So a group added to an instrument, or a whole new instrument type, becomes
    addressable with no edit here. That is the difference between reading the
    catalog and restating it.

    ### Why not AutomationScope

    AutomationScope is a CURATED set - deliberately the parameters worth a curve,
    with `masterEffect` a named gap and every non-automatable parameter absent by
    design. Addressing "full control" through it would have made a channel's
    sample fades, a soundfont's tuning and an effect on the master unreachable,
    and widening it would have broken what the picker offers and what the engine
    resolves.

    The two are related and the relation is stated once, in
    `automationScopeOf` below, so an address that automation accepts is spelled
    the same in both places. A test walks every target availableAutomationTargets
    offers and asserts the address built from it resolves to the same node.
*/
struct ParamAddress
{
    /** "project", "channel", "mixerTrack" or "master". */
    juce::String target;

    /** A channel id or a mixer track id. Unread by "project" and "master",
        which are one node each. */
    int id = 0;

    /** A ParamGroup's jsonKey - "oscillators", "amp", "sample", "soundfont" -
        or "effects", or empty for the target's own parameters. */
    juce::String group;

    /** The position within a repeated group: which oscillator, which effect
        slot. Ignored where the group has one node. */
    int slot = 0;

    juce::Identifier param;

    /** The address, as the sentence an error message needs. */
    juce::String describe() const;
};

/** Reads an address out of a validated argument object.

    The five fields are read with the same names the schema declares, in one
    place, because eight operations take an address and eight hand-rolled reads
    are eight chances to spell `mixerTrack` as `mixertrack` in one of them.
*/
ParamAddress addressFrom (const juce::var& args);

/** The node that carries the property, or an invalid tree.

    Delegates to `automationNodeFor` for every address automation can also
    express, so the tree walk that has to agree with `slotOf` exists once. Only
    the four shapes automation has no scope for - an envelope, a sample, a
    soundfont, and an effect on the master - are resolved here.
*/
juce::ValueTree paramNodeFor (const juce::ValueTree& project, const ParamAddress&);

/** The node a parameter's VALUE is actually stored on.

    The same as paramNodeFor everywhere except an oscillator slot, whose
    parameters are spread over the slot, its generator's node and its LFO's.
    paramNodeFor must keep answering the slot, because that is what an
    automation target addresses and the two spaces are asserted equal; this
    answers the other question, and it is the one a read or a write wants.

    Getting it wrong is silent: the write lands as a property on the OSC node,
    ValueTree accepts it, and the schema drops it on the next save.
*/
juce::ValueTree paramValueNodeFor (const juce::ValueTree& project, const ParamAddress& address);

/** The same descent, given a node already in hand. */
juce::ValueTree paramValueNode (const juce::ValueTree& node, const juce::Identifier& property);

/** What the parameter IS, or nothing when the address names none.

    Read from ModuleCatalog rather than from the automation tables, so it
    answers for every parameter rather than the automatable ones - which is the
    whole reason this address space exists beside AutomationScope's.

    BY VALUE, and that is not a style choice. effectParamsFor returns its table
    by value, so a pointer into it dangles the moment the expression ends -
    the trap paramMenu::Context already records, and the reason it stores its
    spec by value too. Only the automation tables have static storage duration,
    and this has to answer for the others as well.
*/
std::optional<ParamSpec> paramSpecFor (const juce::ValueTree& project, const ParamAddress&);

/** Every parameter this address's node offers, whatever `param` says.

    What a caller lists before it writes, and what makes an unknown parameter
    name an error that can suggest the alternatives rather than one that just
    says no.
*/
std::vector<ParamSpec> paramsAt (const juce::ValueTree& project, const ParamAddress&);

/** The automation scope this address names, or nothing when automation has
    none for it.

    The single statement of how the two address spaces line up. Nothing else in
    this library may map one to the other.
*/
std::optional<AutomationScope> automationScopeOf (const ParamAddress&);

/** The address a target names - the inverse, for reporting a curve's address in
    the terms every other operation takes. */
ParamAddress addressOfTarget (const AutomationTarget&);

} // namespace dew::control
