#include "control/ControlValue.h"
#include "control/ParamAddress.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"

namespace dew::control
{

namespace
{

constexpr const char* kEffects = "effects";

/** The instrument descriptor a channel's stored `source` names, or nullptr.

    instrumentTypeOf returns an optional for the reason instrumentTypeFor does:
    a source this build does not know is not a synth, and treating it as one
    would write a synth's parameters onto a channel that is something else.
*/
const InstrumentDescriptor* descriptorOf (const juce::ValueTree& channel)
{
    if (const auto type = ProjectEdits::instrumentTypeOf (channel))
        return &instrumentDescriptor (*type);

    return nullptr;
}

/** The ParamGroup of this jsonKey on this channel, or nullptr. */
const ParamGroup* groupOf (const juce::ValueTree& channel, const juce::String& jsonKey)
{
    const auto* descriptor = descriptorOf (channel);

    if (descriptor == nullptr)
        return nullptr;

    for (int i = 0; i < descriptor->numGroups; ++i)
        if (jsonKey == descriptor->groups[i].jsonKey)
            return &descriptor->groups[i];

    return nullptr;
}

/** A group's node under a channel.

    A group's node is a child of the CHANNEL or of its INSTRUMENT, and which of
    the two is a fact the tree shape holds rather than the catalog: OSC and AMP
    are the instrument's, SAMPLE and SOUNDFONT are the channel's. Searching both
    rather than encoding it is safe because no node type appears at both levels,
    and it means a group moved between them needs no edit here.
*/
juce::ValueTree groupNode (const juce::ValueTree& channel, const ParamGroup& group, int slot)
{
    const auto find = [&group, slot] (const juce::ValueTree& parent)
    {
        auto index = 0;

        for (const auto& child : parent)
            if (child.hasType (*group.node) && index++ == (group.count > 1 ? slot : 0))
                return child;

        return juce::ValueTree {};
    };

    if (const auto direct = find (channel); direct.isValid())
        return direct;

    return find (channel.getChildWithName (ids::INSTRUMENT));
}

/** The nth EFFECT under any owner. The one walk that must agree with the
    slotOf automation uses, which is why the channel and mixer cases below go
    through automationNodeFor rather than repeating it. */
juce::ValueTree effectAt (const juce::ValueTree& owner, int slot)
{
    auto index = 0;

    for (const auto& child : owner)
        if (child.hasType (ids::EFFECT) && index++ == slot)
            return child;

    return {};
}

juce::ValueTree channelWithId (const juce::ValueTree& project, int id)
{
    return ProjectEdits::findChannel (project, id);
}

juce::ValueTree masterNode (const juce::ValueTree& project)
{
    return project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
}

} // namespace

juce::String ParamAddress::describe() const
{
    juce::String text = target;

    if (target == "channel" || target == "mixerTrack")
        text += " " + juce::String (id);

    if (group.isNotEmpty())
        text += " > " + group + " " + juce::String (slot);

    return text + " > " + param.toString();
}

ParamAddress addressFrom (const juce::var& args)
{
    ParamAddress address;

    address.target = textArg (args, "target", "channel");
    address.id = intArg (args, "id");
    address.group = textArg (args, "group");
    address.slot = intArg (args, "slot");
    address.param = juce::Identifier (textArg (args, "param", "unnamed"));

    return address;
}

std::optional<AutomationScope> automationScopeOf (const ParamAddress& address)
{
    // The whole of how the two address spaces line up. Every case that is
    // absent is absent because automation genuinely has no scope for it - an
    // envelope, a sample, a soundfont and an effect on the master - and
    // AutomationScope's own comment argues each of those.
    if (address.target == "project" && address.group.isEmpty())
        return AutomationScope::project;

    if (address.target == "channel")
    {
        if (address.group.isEmpty())
            return AutomationScope::channel;

        if (address.group == "oscillators")
            return AutomationScope::channelOsc;

        if (address.group == kEffects)
            return AutomationScope::channelEffect;
    }

    if (address.target == "mixerTrack")
    {
        if (address.group.isEmpty())
            return AutomationScope::mixerTrack;

        if (address.group == kEffects)
            return AutomationScope::mixerEffect;
    }

    if (address.target == "master" && address.group.isEmpty())
        return AutomationScope::master;

    return {};
}

ParamAddress addressOfTarget (const AutomationTarget& target)
{
    ParamAddress address;
    address.id = target.targetId;
    address.slot = juce::jmax (0, target.slot);
    address.param = target.property;

    // A switch with no default: -Wswitch-enum is an error under the ci preset,
    // so a scope added to AutomationScope fails to compile until it is given an
    // address, rather than silently reporting itself as a channel.
    switch (target.scope)
    {
        case AutomationScope::project: address.target = "project"; break;
        case AutomationScope::channel: address.target = "channel"; break;
        case AutomationScope::master: address.target = "master"; break;
        case AutomationScope::mixerTrack: address.target = "mixerTrack"; break;

        case AutomationScope::channelOsc:
            address.target = "channel";
            address.group = "oscillators";
            break;

        case AutomationScope::channelEffect:
            address.target = "channel";
            address.group = kEffects;
            break;

        case AutomationScope::mixerEffect:
            address.target = "mixerTrack";
            address.group = kEffects;
            break;
    }

    return address;
}

juce::ValueTree paramNodeFor (const juce::ValueTree& project, const ParamAddress& address)
{
    if (! project.isValid())
        return {};

    // Everything automation can also express resolves through its walk, so the
    // "nth EFFECT under a channel by id" arithmetic exists once and cannot come
    // to mean a different effect here than it does there.
    if (const auto scope = automationScopeOf (address))
        return automationNodeFor (project, *scope, address.id, address.slot);

    if (address.target == "master" && address.group == kEffects)
        return effectAt (masterNode (project), address.slot);

    if (address.target != "channel")
        return {};

    const auto channel = channelWithId (project, address.id);

    if (! channel.isValid())
        return {};

    if (const auto* group = groupOf (channel, address.group))
        return groupNode (channel, *group, address.slot);

    return {};
}

std::vector<ParamSpec> paramsAt (const juce::ValueTree& project, const ParamAddress& address)
{
    const auto node = paramNodeFor (project, address);

    if (! node.isValid())
        return {};

    // An effect's parameters depend on its TYPE and on nothing else, wherever
    // the slot happens to hang.
    if (node.hasType (ids::EFFECT))
    {
        if (const auto type = effectTypeFor (node[ids::type].toString()))
            return effectParamsFor (*type);

        return {};
    }

    if (address.target == "project")
        return projectParamSpecs();

    if (address.target == "mixerTrack")
        return mixerTrackParamSpecs();

    if (address.target == "master")
    {
        // The master has a fader and nothing else - no pan, and none of the
        // state a track carries. A projection of the track's table rather than
        // a table of its own, which is exactly what masterParams() does for
        // automation and for the same reason: the fader's range is stated once.
        std::vector<ParamSpec> gain;

        for (const auto& spec : mixerTrackParamSpecs())
            if (*spec.property == ids::gain)
                gain.push_back (spec);

        return gain;
    }

    const auto channel = channelWithId (project, address.id);

    if (const auto* group = groupOf (channel, address.group))
        return { group->params, group->params + group->numParams };

    return {};
}

std::optional<ParamSpec> paramSpecFor (const juce::ValueTree& project, const ParamAddress& address)
{
    for (const auto& spec : paramsAt (project, address))
        if (*spec.property == address.param)
            return spec;

    return {};
}

} // namespace dew::control
