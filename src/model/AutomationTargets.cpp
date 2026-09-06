#include "model/ParamNames.h"
#include "model/AutomationTargets.h"

#include <map>

#include "model/GeneratorCatalog.h"

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
                                   { AutomationScope::channelAmp, "channelAmp" },
                                   { AutomationScope::channelSoundFont, "channelSoundFont" },
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

const std::vector<ParamSpec>& ampParams()
{
    static const auto specs = automatableIn (ampParamSpecs());
    return specs;
}

const std::vector<ParamSpec>& soundFontParams()
{
    static const auto specs = automatableIn (soundFontParamSpecs());
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

/** Everything automatable on a slot running `generator`.

    The slot's own parameters plus that generator's, which is what the registry
    already says - see GeneratorCatalog.h. It used to be the whole OSC table
    behind a mode check that returned NOTHING for a classic slot, so a classic
    oscillator's `gain` was declared automatable in the catalog and unreachable
    in the picker.
*/
const std::vector<ParamSpec>& oscParams (juce::StringRef generator)
{
    static std::map<juce::String, std::vector<ParamSpec>> byGenerator = []
    {
        std::map<juce::String, std::vector<ParamSpec>> all;

        for (const auto& descriptor : generatorDescriptors())
            all[descriptor.id] = automatableIn (generatorParamSpecs (descriptor.id));

        return all;
    }();

    const auto found = byGenerator.find (generatorFor (generator).id);

    jassert (found != byGenerator.end());
    return found->second;
}

/** Every parameter any generator offers, for the lookup that has only a stored
    property to go on and no slot to ask. */
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
    collect (ampParams());
    collect (soundFontParams());
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
        case AutomationScope::channelAmp: return findIn (ampParams(), property);
        case AutomationScope::channelSoundFont: return findIn (soundFontParams(), property);
        case AutomationScope::mixerTrack: return findIn (mixerTrackParams(), property);
        case AutomationScope::master: return findIn (masterParams(), property);
        case AutomationScope::channelEffect:
        case AutomationScope::mixerEffect: return findIn (effectParams (effectType), property);
    }

    return nullptr;
}

} // namespace dew
