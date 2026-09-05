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

/** The nodes one group covers, in the order the document holds them. */
std::vector<juce::ValueTree> nodesFor (const juce::ValueTree& channel, const ParamGroup& group)
{
    if (*group.node == ids::CHANNEL)
        return { channel };

    // Which node a group lives ON, not which name it happens to have. OSC and
    // AMP hang off the INSTRUMENT child; SAMPLE and SOUNDFONT off the channel
    // itself. This named SAMPLE alone, so capturing a soundfont channel's state
    // looked for SOUNDFONT under INSTRUMENT, found nothing, and returned the
    // defaults - silently, as a preset of a sound nobody made.
    //
    // The apply side was generalised for this reason already; see
    // ProjectEdits::applyInstrumentPreset. It was latent only because nothing
    // in production captured state, which is a thing a preset system that can
    // SAVE stops being true of.
    const auto instrument = channel.getChildWithName (ids::INSTRUMENT);
    const auto parent = instrument.getChildWithName (*group.node).isValid() ? instrument : channel;

    if (group.count <= 1)
        return { parent.getChildWithName (*group.node) };

    std::vector<juce::ValueTree> nodes;

    for (const auto& child : parent)
        if (child.hasType (*group.node) && (int) nodes.size() < group.count)
            nodes.push_back (child);

    return nodes;
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
        warnings.add (path + "." + spec.property->toString() + ": wrong type - using the default");
        return fallback;
    }

    if (spec.control == ParamControl::toggle)
        return coerced;

    if (spec.control == ParamControl::choice)
    {
        for (int i = 0; i < spec.numChoices; ++i)
            if (coerced.toString() == spec.choices[i].id)
                return coerced;

        warnings.add (path + "." + spec.property->toString() + ": \"" + coerced.toString()
                      + "\" is not one of its values - using the default");
        return fallback;
    }

    const auto clamped = spec.clamp ((double) coerced);

    if (! juce::approximatelyEqual (clamped, (double) coerced))
        warnings.add (path + "." + spec.property->toString() + ": " + coerced.toString()
                      + " is outside its range - clamped to " + juce::String (clamped));

    return spec.integral ? juce::var ((int) clamped) : juce::var (clamped);
}

juce::var validateObject (const juce::var& value, const ParamSpec* params, int numParams,
                          const juce::String& path, juce::StringArray& warnings)
{
    auto* source = value.getDynamicObject();

    if (source == nullptr && ! value.isVoid())
        warnings.add (path + ": expected an object - using defaults");

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

                return false;
            }();

            if (! known)
                warnings.add (path + "." + property.name.toString()
                              + ": not a parameter of this type - dropped");
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

juce::var stateFor (const InstrumentDescriptor& descriptor, const juce::ValueTree& channel)
{
    auto* object = new juce::DynamicObject();

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];

        if (! group.inPreset)
            continue;

        const auto nodes = nodesFor (channel, group);

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

    for (int g = 0; g < descriptor.numGroups; ++g)
    {
        const auto& group = descriptor.groups[g];
        const juce::Identifier key (group.jsonKey);
        const juce::String path = "state." + juce::String (group.jsonKey);

        if (! group.inPreset)
        {
            // Reported rather than ignored: a preset carrying a channel's
            // volume was written against a different idea of what a preset is,
            // and loading it silently would move a fader in a finished mix.
            if (source != nullptr && source->hasProperty (key))
                warnings.add (path
                              + ": a preset does not carry the channel's own parameters"
                                " - dropped");

            continue;
        }

        const auto value = source != nullptr ? source->getProperty (key) : juce::var();

        if (group.count <= 1)
        {
            object->setProperty (
                key, validateObject (value, group.params, group.numParams, path, warnings));
            continue;
        }

        juce::Array<juce::var> slots;

        if (const auto* elements = value.getArray())
        {
            for (const auto& element : *elements)
            {
                if (slots.size() >= group.count)
                {
                    warnings.add (path + ": more than " + juce::String (group.count)
                                  + " slots - the rest are dropped");
                    break;
                }

                slots.add (validateObject (element, group.params, group.numParams,
                                           path + "[" + juce::String (slots.size()) + "]",
                                           warnings));
            }
        }
        else if (! value.isVoid())
        {
            warnings.add (path + ": expected an array - using defaults");
        }

        // Every slot the preset did not mention, at its defaults. Without this a
        // one-oscillator preset loaded over a three-oscillator patch would leave
        // the other two sounding, which is the opposite of loading a sound.
        while (slots.size() < group.count)
            slots.add (validateObject ({}, group.params, group.numParams,
                                       path + "[" + juce::String (slots.size()) + "]", warnings));

        object->setProperty (key, slots);
    }

    return juce::var (object);
}

} // namespace dew
