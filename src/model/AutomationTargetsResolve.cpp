// =============================================================================
// The four walks: node -> target, address -> node, clip -> spec, project -> all.
//
// The same header, a second translation unit. AutomationTargets.cpp holds what
// a parameter IS - the scope spellings, the automatable tables and the lookup
// from a property to its spec - and this holds what a PROJECT currently offers,
// which is the half that has to walk a tree.
//
// Split because the file passed the 400-code-line gate, and along the seam the
// header already draws: automationTargetFor and availableAutomationTargets are
// asserted to be two directions of one answer, so they belong together and
// neither belongs beside a static table.
// =============================================================================

#include "model/AutomationTargets.h"

#include "i18n/Strings.h"

#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/TreeWalk.h"
#include "model/ModuleCatalog.h"
#include "model/ParamNames.h"

namespace dew
{

namespace
{

/** "filter" -> "Filter". An effect's stored id is lower case and its display
    name is not; the descriptor knows both, so ask it rather than capitalising
    the id by hand the way this used to. */
juce::String effectLabel (const juce::ValueTree& effect)
{
    if (const auto type = effectTypeFor (effect[ids::type].toString()))
        return effectTypeDisplayName (*type);

    return effect[ids::type].toString();
}

/** Which of its EFFECT siblings this node is. Position, because that is what a
    chain slot IS - an effect carries an id, but the engine addresses the slot. */
int slotOf (const juce::ValueTree& parent, const juce::ValueTree& child,
            const juce::Identifier& type)
{
    int slot = 0;

    for (const auto& sibling : parent)
    {
        if (! sibling.hasType (type))
            continue;

        if (sibling == child)
            return slot;

        ++slot;
    }

    return -1;
}

} // namespace

std::optional<AutomationTarget> automationTargetFor (const juce::ValueTree& project,
                                                     const juce::ValueTree& node,
                                                     const juce::Identifier& property)
{
    juce::ignoreUnused (project);

    if (! node.isValid())
        return {};

    const auto parent = node.getParent();

    AutomationTarget target;
    target.property = property;

    juce::String effectType;
    juce::String ownerName;

    if (node.hasType (ids::PROJECT))
    {
        target.scope = AutomationScope::project;
        target.targetId = 0;
        target.ownerName = tr (StringId::automation_song);
    }
    else if (node.hasType (ids::CHANNEL))
    {
        target.scope = AutomationScope::channel;
        target.targetId = (int) node[ids::id];
        ownerName = node[ids::name].toString();
        target.ownerName = ownerName;
    }
    else if (node.hasType (ids::MIXER_TRACK))
    {
        target.scope = AutomationScope::mixerTrack;
        target.targetId = (int) node[ids::id];
        target.ownerName = node[ids::name].toString();
    }
    else if (node.hasType (ids::MASTER))
    {
        target.scope = AutomationScope::master;
        target.targetId = 0;
        target.ownerName = tr (StringId::automation_master);
    }
    else if (node.hasType (ids::AMP))
    {
        // Off the INSTRUMENT bank, not off the channel - the same parent the
        // OSC slots have.
        const auto channel = parent.getParent();

        if (! parent.hasType (ids::INSTRUMENT) || ! channel.hasType (ids::CHANNEL))
            return {};

        target.scope = AutomationScope::channelAmp;
        target.targetId = (int) channel[ids::id];
        target.ownerName = tr (StringId::automation_parameter,
                               Args {}
                                   .with ("owner", channel[ids::name].toString())
                                   .with ("param", tr (StringId::automation_envelope)));
    }
    else if (node.hasType (ids::SOUNDFONT))
    {
        const auto channel = parent;

        if (! channel.hasType (ids::CHANNEL))
            return {};

        target.scope = AutomationScope::channelSoundFont;
        target.targetId = (int) channel[ids::id];
        target.ownerName = tr (StringId::automation_parameter,
                               Args {}
                                   .with ("owner", channel[ids::name].toString())
                                   .with ("param", tr (StringId::automation_soundFont)));
    }
    else if (node.hasType (ids::OSC) || isOscChildNode (node))
    {
        // A knob built for a parameter one of the slot's child nodes owns is
        // built from that node - generatorNodeFor returns the CLASSIC, the
        // WAVETABLE or the LFO child - so the node arriving here is one level
        // below the slot. Resolving against the slot rather than refusing is what keeps
        // this function and availableAutomationTargets one answer: the picker
        // offers those very targets from the OSC node, so a knob that could not
        // reach them was the picker/control divergence this file's header says
        // the design exists to prevent.
        const auto slot = node.hasType (ids::OSC) ? node : node.getParent();

        if (! slot.hasType (ids::OSC))
            return {};

        // A parameter belonging to a generator this slot is not running means
        // nothing here, so a slot switched back drops out of the picker and
        // leaves any clip pointing at it inert - which is what an effect slot
        // that changes type already does.
        //
        // The parameter, not the whole scope. Gating the scope on the mode is
        // what cost a classic slot its `gain`: the catalog declared it
        // automatable and the picker offered nothing at all.
        if (isForeignGeneratorParam (slot[ids::mode].toString(), property))
            return {};

        const auto bank = slot.getParent();
        const auto channel = bank.getParent();

        if (! channel.hasType (ids::CHANNEL))
            return {};

        target.scope = AutomationScope::channelOsc;
        target.targetId = (int) channel[ids::id];
        target.slot = slotOf (bank, slot, ids::OSC);
        target.ownerName = tr (
            StringId::automation_oscillator,
            Args {}.with ("channel", channel[ids::name].toString()).with ("slot", target.slot + 1));
    }
    else if (node.hasType (ids::EFFECT))
    {
        effectType = node[ids::type].toString();
        target.slot = slotOf (parent, node, ids::EFFECT);

        if (parent.hasType (ids::CHANNEL))
        {
            target.scope = AutomationScope::channelEffect;
            target.targetId = (int) parent[ids::id];
            target.ownerName = tr (StringId::automation_parameter,
                                   Args {}
                                       .with ("owner", parent[ids::name].toString())
                                       .with ("param", effectLabel (node)));
        }
        else if (parent.hasType (ids::MIXER_TRACK))
        {
            target.scope = AutomationScope::mixerEffect;
            target.targetId = (int) parent[ids::id];
            target.ownerName = tr (StringId::automation_parameter,
                                   Args {}
                                       .with ("owner", parent[ids::name].toString())
                                       .with ("param", effectLabel (node)));
        }
        else
        {
            // A MASTER insert. Not automatable, and a NAMED gap rather than an
            // oversight: the picker has never offered one either, because there
            // is no masterEffect scope and no third override struct in the
            // engine to write it into. Saying so here is what stops the next
            // reader assuming it fell through by accident.
            return {};
        }
    }
    else
    {
        // SAMPLE, PATTERN, PLAYLIST_TRACK and the rest. A fade or a transpose is
        // set once for a take rather than moved through it, reversing a sample
        // is a discontinuity in a read pointer rather than a parameter change,
        // and a playlist track has no id to point an automation at.
        return {};
    }

    target.spec = findParamSpec (target.scope, effectType, property);

    if (target.spec == nullptr)
        return {};

    // Composed ONCE, through the message that says what a target is called,
    // rather than assembled from an owner, a separator key and a parameter -
    // which is a sentence built with +, and which PlaylistMenus then had to
    // split back apart on the same separator to group its submenu.
    target.paramName = tr (paramNameOf (*target.spec->property));
    target.displayName = tr (
        StringId::automation_parameter,
        Args {}.with ("owner", target.ownerName).with ("param", target.paramName));
    return target;
}

namespace
{

} // namespace

juce::ValueTree automationNodeFor (const juce::ValueTree& project, AutomationScope scope,
                                   int targetId, int slot)
{
    if (! project.isValid())
        return {};

    // A switch with no default, for the reason DocsSchema.h's identifierOf has
    // none: -Wswitch-enum is an error under the ci preset, so a scope added to
    // the enum fails to compile here until somebody says what node it names.
    // The alternative - an if-chain with a fallthrough - is a scope that
    // silently resolves to nothing and an address that silently does nothing.
    switch (scope)
    {
        case AutomationScope::project: return project;

        case AutomationScope::channel: return tree::childWithId (project, ids::CHANNEL, targetId);

        case AutomationScope::channelAmp:
            return tree::childWithId (project, ids::CHANNEL, targetId)
                .getChildWithName (ids::INSTRUMENT)
                .getChildWithName (ids::AMP);

        case AutomationScope::channelSoundFont:
            return tree::childWithId (project, ids::CHANNEL, targetId)
                .getChildWithName (ids::SOUNDFONT);

        case AutomationScope::mixerTrack:
            return tree::childWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK,
                                      targetId);

        case AutomationScope::master:
            return project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);

        case AutomationScope::channelOsc:
        {
            const auto channel = tree::childWithId (project, ids::CHANNEL, targetId);
            const auto instrument = channel.getChildWithName (ids::INSTRUMENT);

            // getChild, not getChildWithName: the slot is a POSITION among the
            // oscillators, and every one of them has the same type.
            int index = 0;

            for (const auto& osc : instrument)
                if (osc.hasType (ids::OSC) && index++ == slot)
                    return osc;

            return {};
        }

        case AutomationScope::channelEffect:
            return tree::nthChildOfType (tree::childWithId (project, ids::CHANNEL, targetId),
                                         ids::EFFECT, slot);

        case AutomationScope::mixerEffect:
            return tree::nthChildOfType (tree::childWithId (project.getChildWithName (ids::MIXER),
                                                            ids::MIXER_TRACK, targetId),
                                         ids::EFFECT, slot);
    }

    return {};
}

const ParamSpec* specForAutomation (const juce::ValueTree& project,
                                    const juce::ValueTree& automation)
{
    if (! automation.isValid())
        return nullptr;

    const auto scope = automationScopeFromString (automation[ids::scope].toString());
    const juce::Identifier property { automation[ids::param].toString() };

    // Only the two effect scopes need the tree at all: every other scope's
    // parameters are a fixed table, so the property alone answers it.
    juce::String effectType;

    if (scope == AutomationScope::channelEffect || scope == AutomationScope::mixerEffect)
    {
        const auto effect = automationNodeFor (project, scope, (int) automation[ids::targetId],
                                               (int) automation[ids::slot]);

        if (! effect.isValid())
            return nullptr;

        effectType = effect[ids::type].toString();
    }

    return findParamSpec (scope, effectType, property);
}

juce::String automationDisplayName (const juce::ValueTree& project,
                                    const juce::ValueTree& automation)
{
    if (! automation.isValid())
        return {};

    const auto scope = automationScopeFromString (automation[ids::scope].toString());
    const auto node = automationNodeFor (project, scope, (int) automation[ids::targetId],
                                         (int) automation[ids::slot]);
    const juce::Identifier property { automation[ids::param].toString() };

    if (const auto target = automationTargetFor (project, node, property))
        return target->displayName;

    return tr (StringId::playlist_missingAutomation);
}

std::vector<AutomationTarget> availableAutomationTargets (const juce::ValueTree& project)
{
    // A WALK over automationTargetFor rather than a second implementation of it.
    //
    // The owner-node -> (scope, targetId, slot) mapping used to live only inside
    // this loop, so a control had nowhere to ask what it drove - which is why
    // automation was reachable from one button and not from the knob itself.
    // Both directions of one fact now, and a test asserts they agree.
    std::vector<AutomationTarget> targets;

    const auto offer =
        [&targets, &project] (const juce::ValueTree& node, const std::vector<ParamSpec>& specs)
    {
        for (const auto& spec : specs)
            if (auto target = automationTargetFor (project, node, *spec.property))
                targets.push_back (*target);
    };

    const auto offerChain = [&offer] (const juce::ValueTree& owner)
    {
        for (const auto& effect : owner)
            if (effect.hasType (ids::EFFECT))
                offer (effect, effectParams (effect[ids::type].toString()));
    };

    // The song's own group FIRST. The picker groups by the first word of a
    // display name, and a global parameter buried after thirty channels is a
    // parameter nobody finds.
    offer (project, projectParams());

    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        offer (channel, channelParams());

        const auto source = instrumentTypeFor (channel[ids::source].toString());

        // Gated on the SOURCE, all three of them, for one reason: every channel
        // carries every instrument kind's node inert to keep the canonical tree
        // one shape, so an ungated walk advertises targets the engine never
        // renders - an amplitude envelope on a channel playing a recording, or
        // a soundfont offset on a channel with no font.
        if (source == InstrumentType::synth)
        {
            offer (channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP),
                   ampParams());

            for (const auto& osc : channel.getChildWithName (ids::INSTRUMENT))
                if (osc.hasType (ids::OSC))
                    offer (osc, oscParams (osc[ids::mode].toString()));
        }

        if (source == InstrumentType::soundfont)
            offer (channel.getChildWithName (ids::SOUNDFONT), soundFontParams());

        offerChain (channel);
    }

    const auto mixer = project.getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        offer (track, mixerTrackParams());
        offerChain (track);
    }

    offer (mixer.getChildWithName (ids::MASTER), masterParams());

    return targets;
}

} // namespace dew
