#include "AutomationTargets.h"

#include "Ids.h"

namespace dew
{

AutomationScope automationScopeFromString (const juce::String& s)
{
    if (s == "channelEffect") return AutomationScope::channelEffect;
    if (s == "mixerTrack")    return AutomationScope::mixerTrack;
    if (s == "mixerEffect")   return AutomationScope::mixerEffect;
    if (s == "master")        return AutomationScope::master;

    return AutomationScope::channel;
}

juce::String automationScopeToString (AutomationScope scope)
{
    switch (scope)
    {
        case AutomationScope::channelEffect: return "channelEffect";
        case AutomationScope::mixerTrack:    return "mixerTrack";
        case AutomationScope::mixerEffect:   return "mixerEffect";
        case AutomationScope::master:        return "master";
        case AutomationScope::channel:       break;
    }

    return "channel";
}

double mapAutomationValue (const AutomationParamSpec& spec, double normalised)
{
    const auto t = juce::jlimit (0.0, 1.0, normalised);

    if (spec.logarithmic && spec.minimum > 0.0 && spec.maximum > spec.minimum)
        return spec.minimum * std::pow (spec.maximum / spec.minimum, t);

    return spec.minimum + (spec.maximum - spec.minimum) * t;
}

const std::vector<AutomationParamSpec>& channelParams()
{
    static const std::vector<AutomationParamSpec> specs {
        { &ids::volume, "Volume", 0.0, 1.0, false },
        { &ids::pan,    "Pan",   -1.0, 1.0, true },
    };
    return specs;
}

const std::vector<AutomationParamSpec>& mixerTrackParams()
{
    static const std::vector<AutomationParamSpec> specs {
        { &ids::gain, "Gain",  0.0, 1.0, false },
        { &ids::pan,  "Pan",  -1.0, 1.0, true },
    };
    return specs;
}

const std::vector<AutomationParamSpec>& masterParams()
{
    static const std::vector<AutomationParamSpec> specs {
        { &ids::gain, "Gain", 0.0, 1.0, false },
    };
    return specs;
}

const std::vector<AutomationParamSpec>& effectParams (const juce::String& effectType)
{
    static const std::vector<AutomationParamSpec> filter {
        { &ids::cutoff,    "Cutoff",    20.0, 18000.0, false, true },
        { &ids::resonance, "Resonance",  0.05, 4.0,    false },
        { &ids::mix,       "Mix",        0.0,  1.0,    false },
    };

    static const std::vector<AutomationParamSpec> reverb {
        { &ids::roomSize, "Size",    0.0, 1.0, false },
        { &ids::damping,  "Damping", 0.0, 1.0, false },
        { &ids::width,    "Width",   0.0, 1.0, false },
        { &ids::mix,      "Mix",     0.0, 1.0, false },
    };

    static const std::vector<AutomationParamSpec> delay {
        { &ids::delayMs,  "Time",     1.0, 1000.0, false, true },
        { &ids::feedback, "Feedback", 0.0, 0.95,   false },
        { &ids::mix,      "Mix",      0.0, 1.0,    false },
    };

    static const std::vector<AutomationParamSpec> drive {
        { &ids::drive,      "Drive",  1.0, 40.0, false },
        { &ids::outputGain, "Output", 0.0, 4.0,  false },
        { &ids::mix,        "Mix",    0.0, 1.0,  false },
    };

    static const std::vector<AutomationParamSpec> chorus {
        { &ids::rate,  "Rate",  0.01, 20.0, false, true },
        { &ids::depth, "Depth", 0.0,  1.0,  false },
        { &ids::mix,   "Mix",   0.0,  1.0,  false },
    };

    static const std::vector<AutomationParamSpec> eq {
        { &ids::lowGainDb,  "Low",   -24.0, 24.0,   true },
        { &ids::midGainDb,  "Mid",   -24.0, 24.0,   true },
        { &ids::midFreq,    "Freq",  100.0, 8000.0, false, true },
        { &ids::highGainDb, "High",  -24.0, 24.0,   true },
    };

    static const std::vector<AutomationParamSpec> none {};

    if (effectType == "reverb") return reverb;
    if (effectType == "delay")  return delay;
    if (effectType == "drive")  return drive;
    if (effectType == "chorus") return chorus;
    if (effectType == "eq")     return eq;
    if (effectType == "filter") return filter;

    return none;
}

namespace
{

const AutomationParamSpec* findIn (const std::vector<AutomationParamSpec>& specs,
                                   const juce::Identifier& property)
{
    for (const auto& spec : specs)
        if (*spec.property == property)
            return &spec;

    return nullptr;
}

} // namespace

const AutomationParamSpec* findParamSpec (AutomationScope scope, const juce::String& effectType,
                                          const juce::Identifier& property)
{
    switch (scope)
    {
        case AutomationScope::channel:       return findIn (channelParams(), property);
        case AutomationScope::mixerTrack:    return findIn (mixerTrackParams(), property);
        case AutomationScope::master:        return findIn (masterParams(), property);
        case AutomationScope::channelEffect:
        case AutomationScope::mixerEffect:   return findIn (effectParams (effectType), property);
    }

    return nullptr;
}

std::vector<AutomationTarget> availableAutomationTargets (const juce::ValueTree& project)
{
    std::vector<AutomationTarget> targets;

    const auto addChain = [&targets] (const juce::ValueTree& owner, const juce::String& ownerName,
                                      AutomationScope effectScope, int ownerId)
    {
        int slot = 0;

        for (const auto& effect : owner)
        {
            if (! effect.hasType (ids::EFFECT))
                continue;

            const auto effectType = effect[ids::type].toString();

            for (const auto& spec : effectParams (effectType))
                targets.push_back ({ effectScope, ownerId, slot, *spec.property,
                                     ownerName + " > " + effectType.substring (0, 1).toUpperCase()
                                         + effectType.substring (1) + " > " + spec.displayName,
                                     spec.minimum, spec.maximum, spec.logarithmic });

            ++slot;
        }
    };

    for (const auto& channel : project)
    {
        if (! channel.hasType (ids::CHANNEL))
            continue;

        const auto name = channel[ids::name].toString();
        const auto id = (int) channel[ids::id];

        for (const auto& spec : channelParams())
            targets.push_back ({ AutomationScope::channel, id, -1, *spec.property,
                                 name + " > " + spec.displayName,
                                 spec.minimum, spec.maximum, spec.logarithmic });

        addChain (channel, name, AutomationScope::channelEffect, id);
    }

    const auto mixer = project.getChildWithName (ids::MIXER);

    for (const auto& track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        const auto name = track[ids::name].toString();
        const auto id = (int) track[ids::id];

        for (const auto& spec : mixerTrackParams())
            targets.push_back ({ AutomationScope::mixerTrack, id, -1, *spec.property,
                                 name + " > " + spec.displayName,
                                 spec.minimum, spec.maximum, spec.logarithmic });

        addChain (track, name, AutomationScope::mixerEffect, id);
    }

    for (const auto& spec : masterParams())
        targets.push_back ({ AutomationScope::master, 0, -1, *spec.property,
                             juce::String ("Master > ") + spec.displayName,
                             spec.minimum, spec.maximum, spec.logarithmic });

    return targets;
}

} // namespace dew
