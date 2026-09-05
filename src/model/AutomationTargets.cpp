#include "model/ParamNames.h"
#include "model/AutomationTargets.h"

#include <cmath>

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include <array>

namespace dew
{

namespace
{

/** The scopes and their file spellings, as ONE table.

    It was a chain of ifs falling back to `channel` and a switch beside it, so
    adding a scope was a compile error in one direction and a silent
    misreading in the other.
*/
struct ScopeName
{
    AutomationScope scope;
    const char* id;
};

constexpr ScopeName scopeNames[] { { AutomationScope::project, "project" },
                                   { AutomationScope::channel, "channel" },
                                   { AutomationScope::channelOsc, "channelOsc" },
                                   { AutomationScope::channelEffect, "channelEffect" },
                                   { AutomationScope::mixerTrack, "mixerTrack" },
                                   { AutomationScope::mixerEffect, "mixerEffect" },
                                   { AutomationScope::master, "master" } };

} // namespace

AutomationScope automationScopeFromString (const juce::String& s)
{
    for (const auto& row : scopeNames)
        if (s == row.id)
            return row.scope;

    return AutomationScope::channel;
}

juce::String automationScopeToString (AutomationScope scope)
{
    for (const auto& row : scopeNames)
        if (row.scope == scope)
            return row.id;

    return "channel";
}

double automationValueFor (const ParamSpec& spec, double normalised)
{
    const auto t = juce::jlimit (0.0, 1.0, normalised);

    // A discrete parameter has no in-between value. Snapping HERE, in the one
    // function the picker, the point editor, the painter and the engine all
    // call, is what keeps a toggle honest: half on is not a state a bool has,
    // and a filter mode between two modes is not a mode.
    //
    // Equal-width buckets, so a curve at 0.49 is off and one at 0.51 is on.
    if (const auto steps = spec.numDiscreteValues(); steps > 1)
    {
        const auto index = juce::jlimit (0, steps - 1, (int) std::floor (t * (double) steps));

        return spec.minimum + (spec.maximum - spec.minimum) * (double) index / (double) (steps - 1);
    }

    // The spec's own mapping, not a second copy of it. This function restated
    // the logarithm, so adding a third curve would have meant remembering to
    // teach it here as well - and a knob and a drawn curve that disagree about
    // where half travel is are two different parameters wearing one name.
    return spec.fromNormalised (t);
}

namespace
{

/** Everything in `specs` that declares itself automatable.

    Named parameters used to be picked out by hand here, and the comment said
    which ones were worth a curve was an editorial decision. It is - it just
    belongs beside the parameter, where the range and the curve already are,
    rather than in a second list that had already drifted from them.
*/
std::vector<ParamSpec> automatableIn (const std::vector<ParamSpec>& specs)
{
    std::vector<ParamSpec> out;

    for (const auto& spec : specs)
        if (spec.automatable)
            out.push_back (spec);

    return out;
}

} // namespace

const std::vector<ParamSpec>& projectParams()
{
    static const auto specs = automatableIn (projectParamSpecs());
    return specs;
}

const std::vector<ParamSpec>& channelParams()
{
    static const auto specs = automatableIn (channelParamSpecs());
    return specs;
}

const std::vector<ParamSpec>& mixerTrackParams()
{
    static const auto specs = automatableIn (mixerTrackParamSpecs());
    return specs;
}

const std::vector<ParamSpec>& masterParams()
{
    // The master has a fader and nothing else: no pan, and none of the state a
    // track carries. A projection of the track's table rather than a table of
    // its own, so the fader's range is stated once.
    static const auto specs = []
    {
        std::vector<ParamSpec> out;

        for (const auto& spec : mixerTrackParams())
            if (*spec.property == ids::gain)
                out.push_back (spec);

        return out;
    }();

    return specs;
}

const std::vector<ParamSpec>& oscParams()
{
    static const auto specs = automatableIn (oscParamSpecs());
    return specs;
}

const std::vector<ParamSpec>& effectParams (const juce::String& effectType)
{
    static const auto byType = []
    {
        std::array<std::vector<ParamSpec>, (size_t) kNumEffectTypes> built;

        for (const auto& descriptor : effectDescriptors())
            built[(size_t) descriptor.type] = automatableIn (effectParamsFor (descriptor.type));

        return built;
    }();

    static const std::vector<ParamSpec> none {};

    if (const auto type = effectTypeFor (effectType))
        return byType[(size_t) *type];

    return none;
}

juce::StringArray automatableParameterNames()
{
    juce::StringArray names;

    const auto collect = [&names] (const std::vector<ParamSpec>& specs)
    {
        for (const auto& spec : specs)
            if (spec.property != nullptr)
                names.addIfNotAlreadyThere (spec.property->toString());
    };

    collect (projectParams());
    collect (channelParams());
    collect (mixerTrackParams());
    collect (masterParams());
    collect (oscParams());

    for (const auto& descriptor : effectDescriptors())
        collect (effectParams (descriptor.id));

    return names;
}

namespace
{

const ParamSpec* findIn (const std::vector<ParamSpec>& specs, const juce::Identifier& property)
{
    for (const auto& spec : specs)
        if (*spec.property == property)
            return &spec;

    return nullptr;
}

} // namespace

const ParamSpec* findParamSpec (AutomationScope scope, const juce::String& effectType,
                                const juce::Identifier& property)
{
    switch (scope)
    {
        case AutomationScope::project: return findIn (projectParams(), property);
        case AutomationScope::channel: return findIn (channelParams(), property);
        case AutomationScope::channelOsc: return findIn (oscParams(), property);
        case AutomationScope::mixerTrack: return findIn (mixerTrackParams(), property);
        case AutomationScope::master: return findIn (masterParams(), property);
        case AutomationScope::channelEffect:
        case AutomationScope::mixerEffect: return findIn (effectParams (effectType), property);
    }

    return nullptr;
}

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
        target.displayName = tr (StringId::automation_song);
    }
    else if (node.hasType (ids::CHANNEL))
    {
        target.scope = AutomationScope::channel;
        target.targetId = (int) node[ids::id];
        ownerName = node[ids::name].toString();
        target.displayName = ownerName;
    }
    else if (node.hasType (ids::MIXER_TRACK))
    {
        target.scope = AutomationScope::mixerTrack;
        target.targetId = (int) node[ids::id];
        target.displayName = node[ids::name].toString();
    }
    else if (node.hasType (ids::MASTER))
    {
        target.scope = AutomationScope::master;
        target.targetId = 0;
        target.displayName = tr (StringId::automation_master);
    }
    else if (node.hasType (ids::OSC))
    {
        // A wave position on a CLASSIC slot means nothing, so a slot switched
        // back drops out of the picker and leaves any clip pointing at it inert
        // - which is what an effect slot that changes type already does.
        if (node[ids::mode].toString() != "wavetable")
            return {};

        const auto channel = parent.getParent();

        if (! channel.hasType (ids::CHANNEL))
            return {};

        target.scope = AutomationScope::channelOsc;
        target.targetId = (int) channel[ids::id];
        target.slot = slotOf (parent, node, ids::OSC);
        target.displayName = tr (
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
            target.displayName = tr (StringId::automation_parameter,
                                     Args {}
                                         .with ("owner", parent[ids::name].toString())
                                         .with ("param", effectLabel (node)));
        }
        else if (parent.hasType (ids::MIXER_TRACK))
        {
            target.scope = AutomationScope::mixerEffect;
            target.targetId = (int) parent[ids::id];
            target.displayName = tr (StringId::automation_parameter,
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
        // AMP, SAMPLE, PATTERN, PLAYLIST_TRACK and the rest. An envelope stage
        // is not a quantity you move THROUGH a note, and a playlist track has
        // no id to point an automation at.
        return {};
    }

    target.spec = findParamSpec (target.scope, effectType, property);

    if (target.spec == nullptr)
        return {};

    target.displayName += tr (StringId::automation_separator)
                          + tr (paramNameOf (*target.spec->property));
    return target;
}

namespace
{

/** The nth EFFECT under an owner. The inverse of slotOf, and the only thing
    specForAutomation needs the project tree for: an effect's parameters depend
    on its TYPE, and the clip stores a position rather than a type. */
juce::ValueTree effectAt (const juce::ValueTree& owner, int slot)
{
    int index = 0;

    for (const auto& child : owner)
    {
        if (! child.hasType (ids::EFFECT))
            continue;

        if (index == slot)
            return child;

        ++index;
    }

    return {};
}

/** A CHANNEL or a MIXER_TRACK by its id. */
juce::ValueTree ownerWithId (const juce::ValueTree& container, const juce::Identifier& type, int id)
{
    for (const auto& child : container)
        if (child.hasType (type) && (int) child[ids::id] == id)
            return child;

    return {};
}

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

        case AutomationScope::channel: return ownerWithId (project, ids::CHANNEL, targetId);

        case AutomationScope::mixerTrack:
            return ownerWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK, targetId);

        case AutomationScope::master:
            return project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);

        case AutomationScope::channelOsc:
        {
            const auto channel = ownerWithId (project, ids::CHANNEL, targetId);
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
            return effectAt (ownerWithId (project, ids::CHANNEL, targetId), slot);

        case AutomationScope::mixerEffect:
            return effectAt (
                ownerWithId (project.getChildWithName (ids::MIXER), ids::MIXER_TRACK, targetId),
                slot);
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

        for (const auto& osc : channel.getChildWithName (ids::INSTRUMENT))
            if (osc.hasType (ids::OSC))
                offer (osc, oscParams());

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
