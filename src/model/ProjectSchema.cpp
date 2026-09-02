#include "ProjectSchema.h"

namespace dew
{

namespace
{

// --- the schema table --------------------------------------------------------
//
// Declared bottom-up because a parent holds pointers to its children's specs.
// All of these are function-local statics with static storage duration, so the
// pointers stay valid for the life of the process.

const NodeSpec& oscSpec()
{
    static const NodeSpec spec {
        ids::OSC,
        { { ids::wave,        "saw" },
          { ids::octave,      0 },
          { ids::detuneCents, 0.0 },
          { ids::gain,        0.8 } },
        {}
    };
    return spec;
}

const NodeSpec& ampSpec()
{
    static const NodeSpec spec {
        ids::AMP,
        { { ids::attack,  0.005 },
          { ids::decay,   0.120 },
          { ids::sustain, 0.700 },
          { ids::release, 0.150 } },
        {}
    };
    return spec;
}

const NodeSpec& instrumentSpec()
{
    static const NodeSpec spec {
        ids::INSTRUMENT,
        {},
        { { "osc", &oscSpec(), false },
          { "amp", &ampSpec(), false } }
    };
    return spec;
}

const NodeSpec& channelSpec()
{
    static const NodeSpec spec {
        ids::CHANNEL,
        { { ids::id,           1 },
          { ids::name,         "Channel" },
          { ids::colour,       "ff4fa3ff" },
          { ids::mixerTrackId, 1 },
          { ids::basePitch,    60 },
          { ids::volume,       0.8 },
          { ids::pan,          0.0 },
          { ids::muted,        false } },
        { { "instrument", &instrumentSpec(), false } }
    };
    return spec;
}

const NodeSpec& noteSpec()
{
    static const NodeSpec spec {
        ids::NOTE,
        { { ids::ch,          1 },
          { ids::step,        0 },
          { ids::lengthSteps, 1 },
          { ids::pitch,       60 },
          { ids::velocity,    1.0 } },
        {}
    };
    return spec;
}

const NodeSpec& patternSpec()
{
    static const NodeSpec spec {
        ids::PATTERN,
        { { ids::id,          1 },
          { ids::name,        "Pattern 1" },
          { ids::lengthSteps, 16 } },
        { { "notes", &noteSpec(), true } }
    };
    return spec;
}

const NodeSpec& clipSpec()
{
    static const NodeSpec spec {
        ids::CLIP,
        { { ids::patternId,  1 },
          { ids::startBar,   0 },
          { ids::lengthBars, 1 } },
        {}
    };
    return spec;
}

const NodeSpec& playlistTrackSpec()
{
    static const NodeSpec spec {
        ids::PLAYLIST_TRACK,
        { { ids::name, "Track" } },
        { { "clips", &clipSpec(), true } }
    };
    return spec;
}

const NodeSpec& playlistSpec()
{
    static const NodeSpec spec {
        ids::PLAYLIST,
        {},
        { { "tracks", &playlistTrackSpec(), true } }
    };
    return spec;
}

const NodeSpec& masterSpec()
{
    static const NodeSpec spec {
        ids::MASTER,
        { { ids::gain, 0.9 } },
        {}
    };
    return spec;
}

const NodeSpec& mixerTrackSpec()
{
    static const NodeSpec spec {
        ids::MIXER_TRACK,
        { { ids::id,   1 },
          { ids::name, "Insert" },
          { ids::gain, 0.8 },
          { ids::pan,  0.0 },
          { ids::mute, false },
          { ids::solo, false } },
        {}
    };
    return spec;
}

const NodeSpec& mixerSpec()
{
    static const NodeSpec spec {
        ids::MIXER,
        {},
        { { "master", &masterSpec(),     false },
          { "tracks", &mixerTrackSpec(), true } }
    };
    return spec;
}

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

} // namespace

const NodeSpec& projectSpec()
{
    static const NodeSpec spec {
        ids::PROJECT,
        { { ids::formatVersion, kFormatVersion },
          { ids::name,          "Untitled" },
          { ids::tempoBpm,      128.0 },
          { ids::stepsPerBeat,  4 },
          { ids::barsInSong,    16 } },
        { { "channels", &channelSpec(),  true },
          { "patterns", &patternSpec(),  true },
          { "playlist", &playlistSpec(), false },
          { "mixer",    &mixerSpec(),    false } }
    };
    return spec;
}

const NodeSpec& childSpecFor (const NodeSpec& parent, juce::StringRef jsonKey)
{
    for (const auto& child : parent.children)
        if (child.jsonKey == jsonKey)
            return *child.spec;

    jassertfalse; // not a key in this node's schema
    return parent;
}

juce::ValueTree defaultTreeFor (const NodeSpec& spec)
{
    juce::ValueTree tree (spec.type);

    for (const auto& prop : spec.props)
        tree.setProperty (prop.id, prop.defaultValue, nullptr);

    // Only single-object children are materialised; arrays start empty.
    for (const auto& child : spec.children)
        if (! child.isArray)
            tree.appendChild (defaultTreeFor (*child.spec), nullptr);

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
            for (const auto& node : tree)
                if (node.hasType (child.spec->type))
                    out.appendChild (canonicalTree (node, *child.spec), nullptr);
        }
        else
        {
            const auto node = tree.getChildWithName (child.spec->type);
            out.appendChild (canonicalTree (node.isValid() ? node
                                                           : defaultTreeFor (*child.spec),
                                            *child.spec),
                             nullptr);
        }
    }

    return out;
}

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
            object->setProperty (child.jsonKey,
                                 varFromTree (node.isValid() ? node
                                                             : defaultTreeFor (*child.spec),
                                              *child.spec));
        }
    }

    return juce::var (object);
}

juce::ValueTree treeFromVar (const juce::var& value,
                             const NodeSpec& spec,
                             juce::StringArray& warnings,
                             const juce::String& path)
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
            warnings.add (path + "." + key + ": expected "
                          + (prop.defaultValue.isBool()   ? "a boolean"
                             : prop.defaultValue.isInt()  ? "an integer"
                             : prop.defaultValue.isDouble() ? "a number"
                                                            : "a string")
                          + ", got " + object->getProperty (prop.id).toString()
                          + " - using default");
            tree.setProperty (prop.id, prop.defaultValue, nullptr);
        }
    }

    // --- children ------------------------------------------------------------
    for (const auto& child : spec.children)
    {
        const auto childValue = object->getProperty (child.jsonKey);

        if (child.isArray)
        {
            if (const auto* elements = childValue.getArray())
            {
                int index = 0;

                for (const auto& element : *elements)
                    tree.appendChild (treeFromVar (element, *child.spec, warnings,
                                                   path + "." + child.jsonKey
                                                        + "[" + juce::String (index++) + "]"),
                                      nullptr);
            }
            else if (! childValue.isVoid())
            {
                warnings.add (path + "." + child.jsonKey + ": expected an array - ignored");
            }
        }
        else
        {
            tree.appendChild (treeFromVar (childValue, *child.spec, warnings,
                                           path + "." + child.jsonKey),
                              nullptr);
        }
    }

    // --- keys the schema does not know about ---------------------------------
    for (const auto& property : object->getProperties())
    {
        const auto key = property.name.toString();

        const auto isKnownProp = std::any_of (spec.props.begin(), spec.props.end(),
                                              [&] (const auto& p) { return p.id == property.name; });
        const auto isKnownChild = std::any_of (spec.children.begin(), spec.children.end(),
                                               [&] (const auto& c) { return c.jsonKey == key; });

        // "format" is written by the serializer, not the schema, and is checked there.
        if (! isKnownProp && ! isKnownChild && ! (spec.type == ids::PROJECT && key == "format"))
            warnings.add (path + "." + key + ": not part of the schema - dropped");
    }

    return tree;
}

} // namespace dew
