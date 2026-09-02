#include "model/AutomationTargets.h"

#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include <array>

namespace dew
{

AutomationScope automationScopeFromString (const juce::String& s)
{
    if (s == "channelOsc")    return AutomationScope::channelOsc;
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
        case AutomationScope::channelOsc:    return "channelOsc";
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

const std::vector<AutomationParamSpec>& oscParams()
{
    // One row. Position is the only thing on an oscillator worth drawing a
    // curve for: octave and the mode are steps, and detune and gain are set
    // once for a sound rather than moved through it.
    static const std::vector<AutomationParamSpec> specs {
        { &ids::wavePosition, "Position", 0.0, 1.0, false },
    };
    return specs;
}

const std::vector<AutomationParamSpec>& effectParams (const juce::String& effectType)
{
    // A projection of the catalog, not a sixth copy of the same numbers. This
    // table used to restate every effect parameter's range and curve, and had
    // already drifted: it stopped `cutoff` at 18kHz while the engine loaded it
    // to 20kHz, so the top octave of the filter was reachable by hand and not
    // by a curve. The EQ's rows were also in a different order here than in the
    // editor, which is the sort of thing only independent maintenance produces.
    static const auto byType = []
    {
        std::array<std::vector<AutomationParamSpec>, (size_t) kNumEffectTypes> built;

        for (const auto& descriptor : effectDescriptors())
        {
            auto& out = built[(size_t) descriptor.type];

            for (const auto& param : effectParamsFor (descriptor.type))
                if (param.automatable)
                    out.push_back ({ param.property, param.displayName,
                                     param.minimum, param.maximum, param.bipolar,
                                     param.curve == ParamCurve::logarithmic });
        }

        return built;
    }();

    static const std::vector<AutomationParamSpec> none {};

    if (const auto type = effectTypeFor (effectType))
        return byType[(size_t) *type];

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
        case AutomationScope::channelOsc:    return findIn (oscParams(), property);
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

        // Oscillator slots, but only the ones actually running a wavetable. A
        // slot switched back to classic drops out of the picker and leaves any
        // clip pointing at it inert, which is what an effect slot that changes
        // type already does.
        const auto instrument = channel.getChildWithName (ids::INSTRUMENT);
        int oscSlot = 0;

        for (const auto& osc : instrument)
        {
            if (! osc.hasType (ids::OSC))
                continue;

            const auto slot = oscSlot++;

            if (osc[ids::mode].toString() != "wavetable")
                continue;

            for (const auto& spec : oscParams())
                targets.push_back ({ AutomationScope::channelOsc, id, slot, *spec.property,
                                     name + " > Osc " + juce::String (slot + 1) + " > "
                                         + spec.displayName,
                                     spec.minimum, spec.maximum, spec.logarithmic });
        }

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
