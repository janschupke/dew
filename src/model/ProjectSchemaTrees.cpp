// =============================================================================
// Turning a spec into a tree, and a tree into JSON and back.
//
// Split out of ProjectSchema.cpp, which is now the schema TABLE and nothing
// else.
//
// Everything here takes a `const NodeSpec&` and never names a particular one,
// which is what let it move: the table is a graph of function-local statics
// holding pointers into each other and declared bottom-up on purpose, and
// separating it from the four generic walks over it is the whole point.
//
// coerceToTypeOf comes with them. It is what makes a JSON number that should be
// a bool, or a string that should be an int, land as the type the spec declares
// rather than as whatever the file happened to say.
// =============================================================================

#include "model/ProjectSchema.h"

#include "i18n/Strings.h"
#include "model/Ids.h"

namespace dew
{

namespace
{
/** Properties the schema USED to hold, and no longer does.

    A key the schema does not know is normally a typo, a hand-edit or a file
    from a version that is not this one, and saying so is what the warning is
    for. A key the schema DELIBERATELY dropped is none of those: every project
    ever saved carries it, so warning about it would greet the whole existing
    library with a complaint about a change it had no part in.

    `solo` is the first. A track had a mute and a solo, and the two composed -
    which made one track's audibility a fact about every other track in its
    scope, cost three snapshot-wide precomputations, and is why mute could carry
    an automation curve while solo could not. One state per track, and
    shift-clicking its indicator is how "and silence the others" is said.

    A LIST rather than a version check, because the reader has no version to
    check against by the time it is walking properties - and because what this
    encodes is not "old" but "gone", which stays true however many versions pass.
*/
struct RetiredProperty
{
    const juce::Identifier& type;
    const juce::Identifier& property;
};

bool wasRetired (const NodeSpec& spec, const juce::Identifier& key)
{
    static const RetiredProperty retired[] {
        { ids::CHANNEL, ids::solo },
        { ids::PLAYLIST_TRACK, ids::solo },
        { ids::MIXER_TRACK, ids::solo },
    };

    for (const auto& entry : retired)
        if (spec.type == entry.type && key == entry.property)
            return true;

    return false;
}

/** Slot `index` of a fixed-length array child. One helper rather than three
    copies, so the three walkers cannot drift over what an unused slot is.
*/
juce::ValueTree makeArraySlot (const ChildSpec& child, int index)
{
    return child.makeSlot != nullptr ? child.makeSlot (*child.spec, index)
                                     : defaultTreeFor (*child.spec);
}

} // namespace

// --- type-directed coercion --------------------------------------------------

/** Reads `value` as the type of `fallback`. Returns false if the value is
    present but unusable, so the caller can warn and fall back.
*/
bool coerceToTypeOf (const juce::var& fallback, const juce::var& value, juce::var& out)
{
    if (fallback.isBool())
    {
        if (value.isBool() || value.isInt() || value.isDouble())
        {
            out = (bool) value;
            return true;
        }
        return false;
    }

    if (fallback.isInt() || fallback.isInt64())
    {
        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
        {
            out = (int) value;
            return true;
        }
        return false;
    }

    if (fallback.isDouble())
    {
        if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
        {
            out = (double) value;
            return true;
        }
        return false;
    }

    if (fallback.isString())
    {
        // Only accept an actual string: silently stringifying an object or an
        // array would hide a malformed file rather than report it.
        if (value.isString())
        {
            out = value.toString();
            return true;
        }
        return false;
    }

    out = value;
    return true;
}

juce::ValueTree defaultTreeFor (const NodeSpec& spec)
{
    juce::ValueTree tree (spec.type);

    for (const auto& prop : spec.props)
        tree.setProperty (prop.id, prop.defaultValue, nullptr);

    // Single-object children are materialised, and so are fixed-length arrays;
    // variable-length ones start empty.
    for (const auto& child : spec.children)
    {
        if (! child.isArray)
            tree.appendChild (defaultTreeFor (*child.spec), nullptr);
        else
            for (int i = 0; i < child.fixedCount; ++i)
                tree.appendChild (makeArraySlot (child, i), nullptr);
    }

    return tree;
}

juce::ValueTree canonicalTree (const juce::ValueTree& tree, const NodeSpec& spec)
{
    juce::ValueTree out (spec.type);

    for (const auto& prop : spec.props)
        out.setProperty (prop.id, tree.getProperty (prop.id, prop.defaultValue), nullptr);

    for (const auto& child : spec.children)
    {
        if (child.isArray)
        {
            int count = 0;

            for (const auto& node : tree)
            {
                if (! node.hasType (child.spec->type))
                    continue;

                if (child.fixedCount > 0 && count >= child.fixedCount)
                    break;

                out.appendChild (canonicalTree (node, *child.spec), nullptr);
                ++count;
            }

            // A fixed array is always full. A tree assembled by hand has as many
            // slots as the code that built it happened to append, and the editor
            // has to be able to point at all of them.
            for (int i = count; i < child.fixedCount; ++i)
                out.appendChild (makeArraySlot (child, i), nullptr);
        }
        else
        {
            const auto node = tree.getChildWithName (child.spec->type);
            out.appendChild (
                canonicalTree (node.isValid() ? node : defaultTreeFor (*child.spec), *child.spec),
                nullptr);
        }
    }

    return out;
}

namespace
{

/** Whether every property on `tree` is still the default the spec declares.

    Properties only, and deliberately: nothing that carries this flag has
    children, and a recursive answer would be a rule nobody has needed.
*/
bool isAllDefault (const juce::ValueTree& tree, const NodeSpec& spec)
{
    for (const auto& prop : spec.props)
        if (! tree.getProperty (prop.id, prop.defaultValue).equals (prop.defaultValue))
            return false;

    return true;
}

} // namespace

juce::var varFromTree (const juce::ValueTree& tree, const NodeSpec& spec)
{
    auto* object = new juce::DynamicObject();

    for (const auto& prop : spec.props)
        object->setProperty (prop.id, tree.getProperty (prop.id, prop.defaultValue));

    for (const auto& child : spec.children)
    {
        if (child.isArray)
        {
            juce::Array<juce::var> elements;

            for (const auto& node : tree)
                if (node.hasType (child.spec->type))
                    elements.add (varFromTree (node, *child.spec));

            object->setProperty (child.jsonKey, elements);
        }
        else
        {
            const auto node = tree.getChildWithName (child.spec->type);
            const auto written = node.isValid() ? node : defaultTreeFor (*child.spec);

            if (child.omitWhenDefault && isAllDefault (written, *child.spec))
                continue;

            object->setProperty (child.jsonKey, varFromTree (written, *child.spec));
        }
    }

    return juce::var (object);
}

juce::ValueTree treeFromVar (const juce::var& value, const NodeSpec& spec,
                             juce::StringArray& warnings, const juce::String& path)
{
    juce::ValueTree tree (spec.type);

    auto* object = value.getDynamicObject();

    if (object == nullptr)
    {
        if (! value.isVoid())
            warnings.add (path + ": expected an object, got " + value.toString()
                          + " - using defaults");

        return defaultTreeFor (spec);
    }

    // --- properties ----------------------------------------------------------
    for (const auto& prop : spec.props)
    {
        const auto key = prop.id.toString();

        if (! object->hasProperty (prop.id))
        {
            // Absent is normal: an older file simply predates this property.
            tree.setProperty (prop.id, prop.defaultValue, nullptr);
            continue;
        }

        juce::var coerced;

        if (coerceToTypeOf (prop.defaultValue, object->getProperty (prop.id), coerced))
        {
            tree.setProperty (prop.id, coerced, nullptr);
        }
        else
        {
            // A `select` rather than four sentence fragments joined with +:
            // "expected a boolean" is one sentence in English and need not be
            // two words in another, and an article is not detachable from its
            // noun in most languages that have them.
            const auto* expected = prop.defaultValue.isBool()     ? "boolean"
                                   : prop.defaultValue.isInt()    ? "integer"
                                   : prop.defaultValue.isDouble() ? "number"
                                                                  : "string";

            warnings.add (tr (StringId::warning_propertyWrongType,
                              Args {}
                                  .with ("path", path)
                                  .with ("key", key)
                                  .with ("expected", expected)
                                  .with ("value", object->getProperty (prop.id).toString())));
            tree.setProperty (prop.id, prop.defaultValue, nullptr);
        }
    }

    // --- children ------------------------------------------------------------
    for (const auto& child : spec.children)
    {
        const auto childValue = object->getProperty (child.jsonKey);

        if (child.isArray)
        {
            int index = 0;

            if (const auto* elements = childValue.getArray())
            {
                for (const auto& element : *elements)
                {
                    if (child.fixedCount > 0 && index >= child.fixedCount)
                    {
                        warnings.add (path + "." + child.jsonKey + ": more than "
                                      + juce::String (child.fixedCount)
                                      + " entries - the rest are dropped");
                        break;
                    }

                    tree.appendChild (treeFromVar (element, *child.spec, warnings,
                                                   path + "." + child.jsonKey + "["
                                                       + juce::String (index++) + "]"),
                                      nullptr);
                }
            }
            else if (! childValue.isVoid())
            {
                warnings.add (path + "." + child.jsonKey + ": expected an array - ignored");
            }

            // Fill the slots the file did not carry. An older file, or one
            // hand-edited down to a single entry, still loads as a full node.
            for (int i = index; i < child.fixedCount; ++i)
                tree.appendChild (makeArraySlot (child, i), nullptr);
        }
        else
        {
            tree.appendChild (
                treeFromVar (childValue, *child.spec, warnings, path + "." + child.jsonKey),
                nullptr);
        }
    }

    // --- keys the schema does not know about ---------------------------------
    for (const auto& property : object->getProperties())
    {
        const auto key = property.name.toString();

        const auto isKnownProp = std::any_of (spec.props.begin(), spec.props.end(),
                                              [&] (const auto& p)
                                              { return p.id == property.name; });
        const auto isKnownChild = std::any_of (spec.children.begin(), spec.children.end(),
                                               [&] (const auto& c) { return c.jsonKey == key; });

        // "format" is written by the serializer, not the schema, and is checked there.
        // A retired property is dropped in SILENCE - see wasRetired.
        if (! isKnownProp && ! isKnownChild && ! (spec.type == ids::PROJECT && key == "format")
            && ! wasRetired (spec, property.name))
            warnings.add (path + "." + key + ": not part of the schema - dropped");
    }

    return tree;
}
} // namespace dew
