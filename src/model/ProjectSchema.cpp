#include "model/ProjectSchema.h"
#include "model/ModuleCatalog.h"

namespace dew
{

namespace
{

// --- the schema table --------------------------------------------------------
//
// Declared bottom-up because a parent holds pointers to its children's specs.
// All of these are function-local statics with static storage duration, so the
// pointers stay valid for the life of the process.

/** One oscillator slot.

    `enabled` defaults to true because a file written before there were slots
    had exactly one oscillator and it was playing - there is no "enabled" key in
    such a file to say so. The slots the schema materialises alongside it are
    switched off by makeOscillatorSlot.
*/
const NodeSpec& oscSpec()
{
    static const NodeSpec spec {
        ids::OSC,
        { { ids::enabled,     true },
          { ids::wave,        "saw" },
          { ids::octave,      0 },
          { ids::detuneCents, 0.0 },
          { ids::gain,        0.8 },

          // The wavetable half of the slot. Flat beside the classic half rather
          // than a child node of its own, for the reason effectSpec() gives:
          // one declared table stays one walk in the reader, and a mode is a
          // row here instead of a new node type and a new branch.
          //
          // Every one of these has a declared default, so a file written before
          // they existed loads as a classic oscillator with no migration.
          { ids::mode,               "classic" },
          { ids::wavetable,          "basic" },
          { ids::wavePosition,       0.0 },
          { ids::wavePositionMod,    0.0 },
          { ids::wavePositionSource, "envelope" },
          { ids::wavePositionRate,   1.0 },
          { ids::unisonVoices,       1 },
          { ids::unisonDetune,       0.0 } },
        {}
    };
    return spec;
}

/** Oscillator slot `index`, as the schema materialises it.

    Only the first is on. Three oscillators at full gain out of the box would be
    three times the level of every project written before this, and a new
    channel would sound nothing like the one-oscillator instrument the panel
    still opens on.
*/
juce::ValueTree makeOscillatorSlot (const NodeSpec& spec, int index)
{
    // setProperty rather than building a fresh list: it updates the value in
    // place, so every slot's properties stay in the spec's own order. ValueTree
    // equality is order-sensitive, and the demo library is checked with it.
    auto node = defaultTreeFor (spec);
    node.setProperty (ids::enabled, index == 0, nullptr);
    return node;
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
        { { "oscillators", &oscSpec(), true, kMaxOscillators, &makeOscillatorSlot },
          { "amp",         &ampSpec(), false } }
    };
    return spec;
}

/** One effect slot.

    Every parameter of every effect type lives on this one node, each with its
    own default. The file is a little verbose, but the schema stays a single
    declared table with real per-property validation, and adding an effect type
    is a row here rather than a new node type and a new branch in the reader.
*/
const NodeSpec& effectSpec()
{
    // Generated from the catalog rather than restated. Every property below
    // used to be a second copy of a default that also lived in EffectParams, a
    // clamp in the snapshot builder, a range in the automation table and a
    // range in the editor - which is how `cutoff` came to have three different
    // maxima.
    //
    // The ORDER is load-bearing and reproduced deliberately: identifying,
    // then common, then each type's own in EffectType order. ValueTree equality
    // is order-sensitive and the committed examples are byte-compared against
    // what the factory builds, so a different order here is a different file.
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props {
            { ids::id,      1 },
            { ids::type,    "filter" },
            { ids::enabled, true },
        };

        const auto append = [&props] (const ParamSpec& param)
        {
            for (const auto& existing : props)
                if (existing.id == *param.property)
                    return;

            props.push_back ({ *param.property, param.defaultVar() });
        };

        for (const auto& param : commonEffectParams())
            append (param);

        for (const auto& descriptor : effectDescriptors())
            for (int i = 0; i < descriptor.numParams; ++i)
                append (descriptor.params[i]);

        return NodeSpec { ids::EFFECT, std::move (props), {} };
    }();

    return spec;
}

/** The audio source an "audio" channel plays.

    Flat, in the same style as effectSpec(): every parameter lives on the one
    node with a declared default, rather than a node shape that changes with the
    channel kind. A synth channel carries this node too, inert - exactly as
    every channel carries three OSC slots most of which are switched off. That
    is what lets the editor point at a slot before you have committed to using
    it, and it keeps the canonical tree one shape.

    `file` is stored relative to the .dew when the audio sits beside it, so a
    project folder can be copied to another machine intact. See AssetPaths.
*/
const NodeSpec& sampleSpec()
{
    static const NodeSpec spec {
        ids::SAMPLE,
        { { ids::file,             "" },
          { ids::sourceSampleRate, 44100 },
          { ids::lengthSamples,    0 },
          { ids::startSample,      0 },
          // 0 rather than lengthSamples: the trim end has to mean "the end of
          // whatever is there" before the file has been read, and a recording
          // sets its length after the node already exists.
          { ids::endSample,        0 },
          { ids::fadeInMs,         0.0 },
          { ids::fadeOutMs,        0.0 },
          { ids::transpose,        0.0 },
          { ids::reverse,          false },
          { ids::loop,             false } },
        {}
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
          { ids::muted,        false },
          { ids::solo,         false },
          // "synth" or "audio". A discriminator rather than two node types:
          // everything downstream of a channel's mono buffer - pan, volume,
          // the effect chain, mixer routing, metering, automation - is the
          // same for both, and only the source of the samples differs.
          { ids::source,       "synth" },
          // Set when a score compile created this channel, so a later compile
          // finds it again even after it has been renamed. Nothing else about
          // a channel is ever written by a compile.
          { ids::genId,        "" } },
        { { "instrument", &instrumentSpec(), false },
          { "sample",     &sampleSpec(),     false },
          { "effects",    &effectSpec(),     true } }
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
          { ids::lengthSteps, 16 },
          { ids::genId,       "" },
          // What the notes hashed to when the compiler wrote them. Recompiling
          // hashes them again: equal means nobody has touched this pattern and
          // it can be replaced, different means somebody has and it must not
          // be. Without it a recompile is a choice between losing hand edits
          // and never updating anything.
          { ids::genHash,     "" } },
        { { "notes", &noteSpec(), true } }
    };
    return spec;
}

const NodeSpec& pointSpec()
{
    static const NodeSpec spec {
        ids::POINT,
        // A DOUBLE, unlike a note's step, which is an int. A note lands on a
        // step; a curve point is dragged to wherever the pointer was, and
        // coerceToTypeOf drives its conversion off the runtime type of this
        // default - so an int here silently truncated every fractional point
        // on save and moved it back to the last whole step.
        { { ids::step,  0.0 },
          // 0..1 within the target's own range, so a point editor is uniform
          // whatever it is driving.
          { ids::value, 0.5 },
          // Bend between this point and the next: 0 is a straight line,
          // positive holds high longer, negative holds low longer.
          { ids::curve, 0.0 } },
        {}
    };
    return spec;
}

const NodeSpec& automationSpec()
{
    static const NodeSpec spec {
        ids::AUTOMATION,
        { { ids::id,       1 },
          { ids::name,     "Automation" },
          { ids::scope,    "channel" },
          { ids::targetId, 1 },
          { ids::slot,     -1 },
          // The identifier, not "volume". This default names a property, so
          // spelling it out here is a second declaration that a rename cannot
          // follow - which would leave every new automation clip pointing at a
          // parameter that no longer exists, silently.
          { ids::param,    ids::volume.toString() } },
        { { "points", &pointSpec(), true } }
    };
    return spec;
}

const NodeSpec& clipSpec()
{
    static const NodeSpec spec {
        ids::CLIP,
        // `kind` rather than replacing patternId with a generic refId: a
        // version 3 file has clips with no kind at all, and defaulting it to
        // "pattern" is what makes those load unchanged.
        { { ids::kind,         "pattern" },
          { ids::patternId,    1 },
          { ids::automationId, 1 },
          // Which channel an "audio" clip plays, alongside the pattern and
          // automation references. Only the one matching `kind` is meaningful.
          { ids::channelId,    1 },
          { ids::startBar,     0 },
          { ids::lengthBars,   1 },
          { ids::genId,        "" } },
        {}
    };
    return spec;
}

const NodeSpec& playlistTrackSpec()
{
    static const NodeSpec spec {
        ids::PLAYLIST_TRACK,
        { { ids::name,  "Track" },
          { ids::mute,  false },
          { ids::solo,  false },
          { ids::genId, "" } },
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
        // The master is a bus like any other, and every insert could carry a
        // chain while it could not - which read as an omission, not a rule.
        { { "effects", &effectSpec(), true } }
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
        { { "effects", &effectSpec(), true } }
    };
    return spec;
}

/** One line of score source.

    A node per line rather than one string property holding the lot. The
    committed examples are diffed by tests and read by people, and juce::JSON
    writes a newline as \n - so a whole score in one property is a single
    four-kilobyte line that changes entirely whenever a comma moves.
*/
const NodeSpec& lineSpec()
{
    static const NodeSpec spec {
        ids::LINE,
        { { ids::text, "" } },
        {}
    };
    return spec;
}

/** The arrangement language's source, kept with the project it describes.

    Inside the .dew rather than beside it because the two are one document: a
    score and the notes it compiled to disagree the moment either can travel
    without the other, and "which of these two files is current" is not a
    question a musician should ever have to answer.
*/
const NodeSpec& scoreSpec()
{
    static const NodeSpec spec {
        ids::SCORE,
        // The source's own file name, when it came from one. Diagnostics are
        // reported against a name, and "untitled.score:12" is worse than the
        // name the user knows it by.
        { { ids::name, "" } },
        { { "lines", &lineSpec(), true } }
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

/** Slot `index` of a fixed-length array child. One helper rather than three
    copies, so the three walkers cannot drift over what an unused slot is.
*/
juce::ValueTree makeArraySlot (const ChildSpec& child, int index)
{
    return child.makeSlot != nullptr ? child.makeSlot (*child.spec, index)
                                     : defaultTreeFor (*child.spec);
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
          { ids::beatsPerBar,   4 },
          { ids::beatUnit,      4 },
          { ids::barsInSong,    16 } },
        { { "channels",    &channelSpec(),    true },
          { "patterns",    &patternSpec(),    true },
          { "automations", &automationSpec(), true },
          { "playlist",    &playlistSpec(),   false },
          { "mixer",       &mixerSpec(),      false },
          // Last, and permanently so: canonicalTree materialises children in
          // this order and isEquivalentTo compares them in order, so moving
          // this entry would make every committed project unequal to itself.
          { "score",       &scoreSpec(),      false } }
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
                                                   path + "." + child.jsonKey
                                                        + "[" + juce::String (index++) + "]"),
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
