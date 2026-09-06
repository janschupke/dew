#include "model/ModuleState.h"

#include "i18n/Strings.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

namespace
{

/** One parameter, read off a node and brought into range.

    Through ParamSpec::clamp rather than a comparison written here, so a preset
    and the engine's load path agree about what a value out of range becomes.
*/
juce::var readParam (const juce::ValueTree& node, const ParamSpec& spec)
{
    const auto stored = node.getProperty (*spec.property, spec.defaultVar());

    // A choice and a toggle are stored as themselves - a string and a bool -
    // and clamping a string to a numeric range would turn "lowpass" into 0.
    if (spec.control == ParamControl::choice || spec.control == ParamControl::toggle)
        return stored;

    return spec.integral ? juce::var ((int) spec.clamp ((double) stored))
                         : juce::var (spec.clamp ((double) stored));
}

juce::var objectFrom (const juce::ValueTree& node, const ParamSpec* params, int numParams)
{
    auto* object = new juce::DynamicObject();

    for (int i = 0; i < numParams; ++i)
        object->setProperty (*params[i].property, readParam (node, params[i]));

    return juce::var (object);
}

} // namespace

std::vector<juce::ValueTree> nodesFor (const juce::ValueTree& channel, const ParamGroup& group)
{
    if (*group.node == ids::CHANNEL)
        return { channel };

    const auto instrument = channel.getChildWithName (ids::INSTRUMENT);

    // One per node of the group it sits UNDER: a generator's parameters hang
    // off each oscillator slot. Written here rather than as a second walk
    // beside the first, which is what made the two paths drift before.
    if (group.under != nullptr)
    {
        std::vector<juce::ValueTree> nodes;

        for (const auto& owner : instrument)
            if (owner.hasType (*group.under) && (int) nodes.size() < group.count)
                nodes.push_back (owner.getChildWithName (*group.node));

        return nodes;
    }

    // Which node a group lives ON, not which name it happens to have. OSC and
    // AMP hang off the INSTRUMENT child; SAMPLE and SOUNDFONT off the channel
    // itself. This named SAMPLE alone, so capturing a soundfont channel's state
    // looked for SOUNDFONT under INSTRUMENT, found nothing, and returned the
    // defaults - silently, as a preset of a sound nobody made.
    const auto parent = instrument.getChildWithName (*group.node).isValid() ? instrument : channel;

    if (group.count <= 1)
        return { parent.getChildWithName (*group.node) };

    std::vector<juce::ValueTree> nodes;

    for (const auto& child : parent)
        if (child.hasType (*group.node) && (int) nodes.size() < group.count)
            nodes.push_back (child);

    return nodes;
}

namespace
{

/** The dotted path a warning names, for one parameter of one node.

    Three of the warnings below spelled `path + "." + property` themselves, and
    the dot is not part of any sentence - it is the path's own punctuation, and
    the path is what the message interpolates.
*/
juce::String propertyPath (const juce::String& path, const ParamSpec& spec)
{
    return path + "." + spec.property->toString();
}

/** One parameter out of a var object, coerced, clamped and reported. */
juce::var validateParam (const juce::DynamicObject* object, const ParamSpec& spec,
                         const juce::String& path, juce::StringArray& warnings)
{
    const auto fallback = spec.defaultVar();

    if (object == nullptr || ! object->hasProperty (*spec.property))
        return fallback;

    juce::var coerced;

    if (! coerceToTypeOf (fallback, object->getProperty (*spec.property), coerced))
    {
        warnings.add (tr (StringId::warning_propertyWrongTypeAt,
                          Args {}.with ("path", propertyPath (path, spec))));
        return fallback;
    }

    if (spec.control == ParamControl::toggle)
        return coerced;

    if (spec.control == ParamControl::choice)
    {
        for (int i = 0; i < spec.numChoices; ++i)
            if (coerced.toString() == spec.choices[i].id)
                return coerced;

        warnings.add (tr (
            StringId::warning_propertyNotAChoice,
            Args {}.with ("path", propertyPath (path, spec)).with ("value", coerced.toString())));
        return fallback;
    }

    const auto clamped = spec.clamp ((double) coerced);

    if (! juce::approximatelyEqual (clamped, (double) coerced))
        warnings.add (
            tr (StringId::warning_propertyClamped, Args {}
                                                       .with ("path", propertyPath (path, spec))
                                                       .with ("value", coerced.toString())
                                                       .with ("clamped", clamped)));

    return spec.integral ? juce::var ((int) clamped) : juce::var (clamped);
}

/** @param nested  keys this object legitimately holds that are not parameters:
                   a slot element carries one per generator, the way the
                   project file does. Reported as unknown without it, which is
                   what every shipped synth preset then warned about. */
juce::var validateObject (const juce::var& value, const ParamSpec* params, int numParams,
                          const juce::String& path, juce::StringArray& warnings,
                          const juce::StringArray& nested = {})
{
    auto* source = value.getDynamicObject();

    if (source == nullptr && ! value.isVoid())
        warnings.add (tr (StringId::warning_expectedObject, Args {}.with ("path", path)));

    auto* object = new juce::DynamicObject();

    for (int i = 0; i < numParams; ++i)
        object->setProperty (*params[i].property,
                             validateParam (source, params[i], path, warnings));

    // Anything the type does not declare is dropped and said so, the way the
    // schema reports a key it does not know.
    if (source != nullptr)
        for (const auto& property : source->getProperties())
        {
            const auto known = [&]
            {
                for (int i = 0; i < numParams; ++i)
                    if (*params[i].property == property.name)
                        return true;

                return nested.contains (property.name.toString());
            }();

            if (! known)
                warnings.add (tr (StringId::warning_notAParameter,
                                  Args {}.with ("path", path + "." + property.name.toString())));
        }

    return juce::var (object);
}

} // namespace

juce::var stateFor (const EffectDescriptor& descriptor, const juce::ValueTree& effect)
{
    // effectParamsFor rather than the descriptor's own table, because `mix` is
    // a parameter of every type and is the difference between real presets: a
    // drive at mix 0.35 is parallel saturation and the same drive at 1.0 is
    // not. `id` and `enabled` are absent - see Preset.h.
    const auto params = effectParamsFor (descriptor.type);

    return objectFrom (effect, params.data(), (int) params.size());
}

const ParamGroup* groupOn (const InstrumentDescriptor& descriptor, const juce::Identifier& node)
{
    for (int i = 0; i < descriptor.numGroups; ++i)
        if (*descriptor.groups[i].node == node)
            return &descriptor.groups[i];

    return nullptr;
}

juce::var stateFor (const InstrumentDescriptor& descriptor, const juce::ValueTree& channel)
{
    auto* object = new juce::DynamicObject();

    // Owners first, then the groups that nest inside them: a generator's
    // parameters are written INTO the slot element they belong to, so a
    // preset's body is shaped exactly like the project file's - which is what
    // lets one be read straight out of the other, and what Preset.h promises.
    for (const auto nested : { false, true })
    {
        for (int g = 0; g < descriptor.numGroups; ++g)
        {
            const auto& group = descriptor.groups[g];

            if (! group.inPreset || (group.under != nullptr) != nested)
                continue;

            const auto nodes = nodesFor (channel, group);

            if (nested)
            {
                const auto* owner = groupOn (descriptor, *group.under);

                if (owner == nullptr)
                    continue;

                auto* slots = object->getProperty (juce::Identifier (owner->jsonKey)).getArray();

                if (slots == nullptr)
                    continue;

                for (int i = 0; i < slots->size() && (size_t) i < nodes.size(); ++i)
                    if (auto* slot = (*slots)[i].getDynamicObject())
                        slot->setProperty (
                            juce::Identifier (group.jsonKey),
                            objectFrom (nodes[(size_t) i], group.params, group.numParams));

                continue;
            }

            if (group.count <= 1)
            {
                object->setProperty (juce::Identifier (group.jsonKey),
                                     objectFrom (nodes.empty() ? juce::ValueTree() : nodes.front(),
                                                 group.params, group.numParams));
                continue;
            }

            juce::Array<juce::var> slots;

            for (const auto& node : nodes)
                slots.add (objectFrom (node, group.params, group.numParams));

            object->setProperty (juce::Identifier (group.jsonKey), slots);
        }
    }

    return juce::var (object);
}

juce::var validateState (const EffectDescriptor& descriptor, const juce::var& state,
                         juce::StringArray& warnings)
{
    const auto params = effectParamsFor (descriptor.type);

    return validateObject (state, params.data(), (int) params.size(), "state", warnings);
}

juce::var validateState (const InstrumentDescriptor& descriptor, const juce::var& state,
                         juce::StringArray& warnings)
{
    auto* source = state.getDynamicObject();

    if (source == nullptr && ! state.isVoid())
        warnings.add (tr (StringId::warning_stateNotAnObject));

    auto* object = new juce::DynamicObject();

    // The keys a group's elements may carry that are not its own parameters:
    // one per group that nests inside it.
    const auto nestedIn = [&descriptor] (const juce::Identifier& node)
    {
        juce::StringArray keys;

        for (int i = 0; i < descriptor.numGroups; ++i)
            if (descriptor.groups[i].under != nullptr && *descriptor.groups[i].under == node)
                keys.add (descriptor.groups[i].jsonKey);

        return keys;
    };

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];
        const juce::Identifier key (group.jsonKey);
        const juce::String path = "state." + juce::String (group.jsonKey);

        // A nested group is validated as part of the element it sits in - see
        // the second pass below - so it has no top-level key of its own.
        if (group.under != nullptr)
            continue;

        if (! group.inPreset)
        {
            // Reported rather than ignored: a preset carrying a channel's
            // volume was written against a different idea of what a preset is,
            // and loading it silently would move a fader in a finished mix.
            if (source != nullptr && source->hasProperty (key))
                warnings.add (
                    tr (StringId::warning_presetHasNoChannelParams, Args {}.with ("path", path)));

            continue;
        }

        const auto value = source != nullptr ? source->getProperty (key) : juce::var();

        if (group.count <= 1)
        {
            object->setProperty (key, validateObject (value, group.params, group.numParams, path,
                                                      warnings, nestedIn (*group.node)));
            continue;
        }

        juce::Array<juce::var> slots;

        if (const auto* elements = value.getArray())
        {
            for (const auto& element : *elements)
            {
                if (slots.size() >= group.count)
                {
                    warnings.add (tr (StringId::warning_tooManySlots,
                                      Args {}.with ("path", path).count (group.count)));
                    break;
                }

                slots.add (validateObject (element, group.params, group.numParams,
                                           path + "[" + juce::String (slots.size()) + "]", warnings,
                                           nestedIn (*group.node)));
            }
        }
        else if (! value.isVoid())
        {
            warnings.add (tr (StringId::warning_expectedArray, Args {}.with ("path", path)));
        }

        // Every slot the preset did not mention, at its defaults. Without this a
        // one-oscillator preset loaded over a three-oscillator patch would leave
        // the other two sounding, which is the opposite of loading a sound.
        while (slots.size() < group.count)
            slots.add (validateObject ({}, group.params, group.numParams,
                                       path + "[" + juce::String (slots.size()) + "]", warnings,
                                       nestedIn (*group.node)));

        object->setProperty (key, slots);
    }

    // The generators, into the slot elements just built. Second, because a
    // nested group has nowhere to go until its owner's array exists.
    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];

        if (group.under == nullptr || ! group.inPreset)
            continue;

        const auto* owner = groupOn (descriptor, *group.under);

        if (owner == nullptr)
            continue;

        auto* slots = object->getProperty (juce::Identifier (owner->jsonKey)).getArray();

        if (slots == nullptr)
            continue;

        const juce::Identifier key (group.jsonKey);
        const auto* sourceSlots = source != nullptr
                                      ? source->getProperty (juce::Identifier (owner->jsonKey))
                                            .getArray()
                                      : nullptr;

        for (int i = 0; i < slots->size(); ++i)
        {
            auto* slot = (*slots)[i].getDynamicObject();

            if (slot == nullptr)
                continue;

            const auto path = "state." + juce::String (owner->jsonKey) + "[" + juce::String (i)
                              + "]." + juce::String (group.jsonKey);

            auto value = juce::var();

            if (sourceSlots != nullptr && i < sourceSlots->size())
                if (const auto* sourceSlot = (*sourceSlots)[i].getDynamicObject())
                    value = sourceSlot->getProperty (key);

            slot->setProperty (
                key, validateObject (value, group.params, group.numParams, path, warnings));
        }
    }

    return juce::var (object);
}

} // namespace dew
