#include <cmath>

#include "control/ControlValue.h"

namespace dew::control
{

const char* nameOfKind (ValueKind kind) noexcept
{
    // A switch with no default. -Wswitch-enum is an error under the ci preset,
    // so a ValueKind added to ControlTypes.h fails to compile here until
    // somebody says how it is spelled - which is also how it reaches the tool
    // schema and the website, so the two cannot drift.
    switch (kind)
    {
        case ValueKind::text: return "text";
        case ValueKind::integer: return "integer";
        case ValueKind::number: return "number";
        case ValueKind::flag: return "flag";
        case ValueKind::object: return "object";
        case ValueKind::array: return "array";
        case ValueKind::any: return "any";
    }

    return "unknown";
}

namespace
{

/** juce::var's own predicates, with the one distinction it does not draw.

    isInt() is false for a var holding 3.0, and every JSON reader in existence
    parses `3` to a double when it feels like it. So an integer argument accepts
    a number with no fractional part rather than the C++ type juce::JSON
    happened to choose, which is the difference between a tool that works and a
    tool that rejects `"step": 4`.
*/
bool looksNumeric (const juce::var& v)
{
    return v.isInt() || v.isInt64() || v.isDouble();
}

bool looksIntegral (const juce::var& v)
{
    if (v.isInt() || v.isInt64())
        return true;

    if (! v.isDouble())
        return false;

    const auto d = (double) v;
    return juce::exactlyEqual (d, std::floor (d));
}

juce::String describe (const juce::var& v)
{
    if (v.isVoid() || v.isUndefined())
        return "nothing";

    if (v.isString())
        return "a string";

    if (v.isBool())
        return "a boolean";

    if (looksNumeric (v))
        return "a number";

    if (v.isArray())
        return "an array";

    if (v.getDynamicObject() != nullptr)
        return "an object";

    return "something else";
}

juce::String join (const juce::String& path, const juce::String& name)
{
    return path.isEmpty() ? name : path + "." + name;
}

juce::String checkShape (const std::vector<ArgSpec>& shape, const juce::var& args,
                         const juce::String& path);

/** One value against one declared kind. */
juce::String checkValue (const ArgSpec& spec, const juce::var& value, const juce::String& where)
{
    const auto wrong = [&where, &value] (const char* wanted)
    { return where + " must be " + wanted + ", but it is " + describe (value) + "."; };

    switch (spec.kind)
    {
        case ValueKind::text: return value.isString() ? juce::String() : wrong ("a string");

        case ValueKind::integer:
            return looksIntegral (value) ? juce::String() : wrong ("a whole number");

        case ValueKind::number: return looksNumeric (value) ? juce::String() : wrong ("a number");

        case ValueKind::flag: return value.isBool() ? juce::String() : wrong ("true or false");

        case ValueKind::any:
            // Anything but absence, which the caller above has already handled.
            // What this value may actually be is the addressed parameter's
            // business, and it says so in a sentence naming the parameter -
            // which is a better error than one naming a JSON type.
            return {};

        case ValueKind::object:
            if (value.getDynamicObject() == nullptr)
                return wrong ("an object");

            return checkShape (spec.fields, value, where);

        case ValueKind::array:
        {
            if (! value.isArray())
                return wrong ("an array");

            const auto* entries = value.getArray();

            for (int i = 0; i < entries->size(); ++i)
            {
                const auto& entry = entries->getReference (i);
                const auto at = where + "[" + juce::String (i) + "]";

                // An array of bare values checks each ELEMENT against the one
                // unnamed field, rather than treating it as an object.
                if (spec.holdsBareElements())
                {
                    const auto fault = checkValue (spec.fields.front(), entry, at);

                    if (fault.isNotEmpty())
                        return fault;

                    continue;
                }

                if (entry.getDynamicObject() == nullptr)
                    return at + " must be an object, but it is " + describe (entry) + ".";

                const auto fault = checkShape (spec.fields, entry, at);

                if (fault.isNotEmpty())
                    return fault;
            }

            return {};
        }
    }

    return {};
}

juce::String checkShape (const std::vector<ArgSpec>& shape, const juce::var& args,
                         const juce::String& path)
{
    auto* object = args.getDynamicObject();

    if (object == nullptr)
        return path.isEmpty() ? juce::String ("the arguments must be an object.")
                              : path + " must be an object.";

    const auto& properties = object->getProperties();

    // A key nothing declares, first: a caller who misspelled one is better told
    // that than told a required argument is missing, which is the same fault
    // wearing the other name.
    for (int i = 0; i < properties.size(); ++i)
    {
        const auto given = properties.getName (i).toString();
        auto known = false;

        for (const auto& spec : shape)
            if (given == spec.name)
                known = true;

        if (! known)
        {
            juce::StringArray offered;

            for (const auto& spec : shape)
                offered.add (spec.name);

            return join (path, given) + " is not an argument of this operation. It " + "takes "
                   + (offered.isEmpty() ? juce::String ("none") : offered.joinIntoString (", "))
                   + ".";
        }
    }

    for (const auto& spec : shape)
    {
        const auto where = join (path, spec.name);
        const auto value = object->getProperty (juce::Identifier (spec.name));

        if (value.isVoid() || value.isUndefined())
        {
            if (spec.required)
                return where + " is required.";

            continue;
        }

        const auto fault = checkValue (spec, value, where);

        if (fault.isNotEmpty())
            return fault;
    }

    return {};
}

} // namespace

juce::String validateArgs (const std::vector<ArgSpec>& shape, const juce::var& args)
{
    // An absent `arguments` member is within the protocol, and is exactly what a
    // client sends for a tool it need pass nothing to. Refusing it would make
    // every such tool unreachable from a conforming caller - and that is not
    // only the no-argument ones: a tool whose arguments are ALL optional, like
    // render_status or score_read, is equally callable with nothing at all.
    if (args.isVoid() || args.isUndefined())
    {
        for (const auto& spec : shape)
            if (spec.required)
                return juce::String (spec.name) + " is required.";

        return {};
    }

    return checkShape (shape, args, {});
}

const juce::Array<juce::var>& emptyArray()
{
    static const juce::Array<juce::var> none;
    return none;
}

bool hasArg (const juce::var& args, const juce::Identifier& name)
{
    auto* object = args.getDynamicObject();

    if (object == nullptr)
        return false;

    const auto value = object->getProperty (name);
    return ! (value.isVoid() || value.isUndefined());
}

juce::String textArg (const juce::var& args, const juce::Identifier& name,
                      const juce::String& fallback)
{
    return hasArg (args, name) ? args[name].toString() : fallback;
}

int intArg (const juce::var& args, const juce::Identifier& name, int fallback)
{
    return hasArg (args, name) ? (int) (double) args[name] : fallback;
}

double numberArg (const juce::var& args, const juce::Identifier& name, double fallback)
{
    return hasArg (args, name) ? (double) args[name] : fallback;
}

bool flagArg (const juce::var& args, const juce::Identifier& name, bool fallback)
{
    return hasArg (args, name) ? (bool) args[name] : fallback;
}

const juce::Array<juce::var>& arrayArg (const juce::var& args, const juce::Identifier& name)
{
    if (! hasArg (args, name))
        return emptyArray();

    const auto* entries = args[name].getArray();

    return entries != nullptr ? *entries : emptyArray();
}

} // namespace dew::control
