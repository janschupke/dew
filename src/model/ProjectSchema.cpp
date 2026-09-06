#include "model/ProjectSchema.h"

#include <map>

#include "model/GeneratorCatalog.h"
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

/** The declared parameters of one catalog group, as schema properties.

    The same argument effectSpec() makes, applied to the other kind of module:
    every one of these used to be a second copy of a default that also lived in
    the catalog, a clamp in the snapshot builder, a range in the automation
    table and a range in a knob.

    The ORDER is the group's, and is load-bearing in both directions - the
    catalog's tables carry a comment saying so. ValueTree property order follows
    this, and a demo test compares the committed examples byte for byte against
    what the factory writes, so a reshuffle of a catalog table is a rewrite of
    every .dew in the repository and fails there.
*/
void appendGroup (std::vector<PropSpec>& props, const ParamGroup& group)
{
    for (int i = 0; i < group.numParams; ++i)
        props.push_back ({ *group.params[i].property, group.params[i].defaultVar() });
}

/** The group a descriptor declares for one node type. */
const ParamGroup& groupFor (const InstrumentDescriptor& descriptor, const juce::Identifier& node)
{
    for (int i = 0; i < descriptor.numGroups; ++i)
        if (*descriptor.groups[i].node == node)
            return descriptor.groups[i];

    // A node the descriptor does not declare is a table error, not a document
    // one, and returning an empty group would generate a node with no
    // properties rather than saying so.
    jassertfalse;
    static const ParamGroup empty { &node, "", StringId::group_none_name, nullptr, 0 };
    return empty;
}

const ParamGroup& synthGroup (const juce::Identifier& node)
{
    return groupFor (instrumentDescriptor (InstrumentType::synth), node);
}

/** One oscillator slot.

    `enabled` defaults to true because a file written before there were slots
    had exactly one oscillator and it was playing - there is no "enabled" key in
    such a file to say so. The slots the schema materialises alongside it are
    switched off by makeOscillatorSlot.
*/
/** One generator's parameters, under a slot.

    A node per generator rather than one flat run of every generator's
    parameters, which is what this was. Both are always present and one of them
    is inert - the shape every CHANNEL already has, carrying a SAMPLE and a
    SOUNDFONT whichever kind it is - so the tree stays one shape and the editor
    can point at a generator before you have committed to it.

    What that buys over the flat node is that a classic slot stops storing the
    seven wavetable properties it never reads. Twelve of them per example file,
    on slots that were switched off.
*/
const NodeSpec& generatorSpec (juce::StringRef generator)
{
    static std::map<juce::String, NodeSpec> specs;

    auto found = specs.find (juce::String (generator));

    if (found == specs.end())
    {
        const auto& descriptor = generatorFor (generator);

        std::vector<PropSpec> props;

        for (int i = 0; i < descriptor.numParams; ++i)
            props.push_back ({ *descriptor.params[i].property, descriptor.params[i].defaultVar() });

        // The node the DESCRIPTOR names, so the mapping from a generator to
        // its node is stated once - see GeneratorCatalog.h.
        found = specs
                    .emplace (juce::String (generator),
                              NodeSpec { *descriptor.node, std::move (props), {} })
                    .first;
    }

    return found->second;
}

const NodeSpec& classicSpec()
{
    return generatorSpec ("classic");
}

const NodeSpec& wavetableSpec()
{
    return generatorSpec ("wavetable");
}

const NodeSpec& oscSpec()
{
    // The SLOT's own five - whether it is on, its octave, its detune, its gain
    // and which generator it runs - and then a node per generator.
    //
    // `enabled` defaults to true because a file written before there were slots
    // had exactly one oscillator and it was playing. The slots the schema
    // materialises alongside it are switched off by makeOscillatorSlot.
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props;
        appendGroup (props, synthGroup (ids::OSC));

        return NodeSpec { ids::OSC,
                          std::move (props),
                          { { "classic", &classicSpec(), false, 0, nullptr,
                              /*omitWhenDefault*/ true },
                            { "wavetable", &wavetableSpec(), false, 0, nullptr,
                              /*omitWhenDefault*/ true } } };
    }();

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
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props;
        appendGroup (props, synthGroup (ids::AMP));

        return NodeSpec { ids::AMP, std::move (props), {} };
    }();

    return spec;
}

const NodeSpec& instrumentSpec()
{
    static const NodeSpec spec { ids::INSTRUMENT,
                                 {},
                                 { { "oscillators", &oscSpec(), true, kMaxOscillators,
                                     &makeOscillatorSlot },
                                   { "amp", &ampSpec(), false } } };
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
            { ids::id, 1 },
            { ids::type, "filter" },
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
    // Spliced rather than wholly generated: the first five are what was FOUND
    // in the recording - where it lives, its rate, its length and its trim -
    // and the catalog declares only the parameters somebody sets. A frame count
    // has no range, no default worth a knob and no meaning on another take.
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props {
            { ids::file, "" },
            { ids::sourceSampleRate, 44100 },
            { ids::lengthSamples, 0 },
            { ids::startSample, 0 },
            // 0 rather than lengthSamples: the trim end has to mean "the end of
            // whatever is there" before the file has been read, and a recording
            // sets its length after the node already exists.
            { ids::endSample, 0 },
        };

        appendGroup (props, groupFor (instrumentDescriptor (InstrumentType::audio), ids::SAMPLE));

        return NodeSpec { ids::SAMPLE, std::move (props), {} };
    }();

    return spec;
}

/** The soundfont a "soundfont" channel plays.

    Built the way sampleSpec() is, and spliced the same way: the first four
    properties say WHICH sound - a path, a bank, a program, and the name it had
    when it was chosen - and the catalog declares the handful of offsets a
    person sets. A synth channel carries this node too, inert, exactly as every
    channel already carries a SAMPLE it may never use.

    `presetName` is stored rather than derived so a channel can still say what
    it was pointed at on a machine that does not have the font. The other three
    are here rather than in the catalog for the reason a sample's `file` is:
    they say which sound, not what is done to it, so they do not belong in a
    preset.
*/
const NodeSpec& soundFontSpec()
{
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props {
            { ids::file, "" },
            { ids::bank, 0 },
            { ids::program, 0 },
            { ids::presetName, "" },
        };

        appendGroup (props,
                     groupFor (instrumentDescriptor (InstrumentType::soundfont), ids::SOUNDFONT));

        return NodeSpec { ids::SOUNDFONT, std::move (props), {} };
    }();

    return spec;
}

const NodeSpec& channelSpec()
{
    static const NodeSpec spec = []
    {
        std::vector<PropSpec> props {
            { ids::id, 1 },
            { ids::name, "Channel" },
            { ids::colour, "ff4fa3ff" },
            { ids::mixerTrackId, 1 },
        };

        // The channel's own parameters - base pitch, level, pan, mute.
        // Declared by the instrument descriptor, which marks them as NOT a
        // preset's: they are the instrument's parameters but they are not its
        // sound.
        appendGroup (props, synthGroup (ids::CHANNEL));

        // "synth" or "audio". A discriminator rather than two node types:
        // everything downstream of a channel's mono buffer - pan, volume,
        // the effect chain, mixer routing, metering, automation - is the
        // same for both, and only the source of the samples differs.
        props.push_back ({ ids::source, "synth" });

        // Set when a score compile created this channel, so a later compile
        // finds it again even after it has been renamed. Nothing else about
        // a channel is ever written by a compile.
        //
        // Hand-written rather than a catalog row, like `id` and `name` above:
        // it identifies the channel rather than describing its sound, so it has
        // no range, nothing turns it, and a preset must never carry it.
        props.push_back ({ ids::genId, "" });

        return NodeSpec { ids::CHANNEL,
                          std::move (props),
                          { { "instrument", &instrumentSpec(), false },
                            { "sample", &sampleSpec(), false },
                            { "soundfont", &soundFontSpec(), false },
                            { "effects", &effectSpec(), true } } };
    }();

    return spec;
}

const NodeSpec& noteSpec()
{
    static const NodeSpec spec { ids::NOTE,
                                 { { ids::ch, 1 },
                                   { ids::step, 0 },
                                   { ids::lengthSteps, 1 },
                                   { ids::pitch, 60 },
                                   { ids::velocity, 1.0 } },
                                 {} };
    return spec;
}

const NodeSpec& patternSpec()
{
    static const NodeSpec spec { ids::PATTERN,
                                 { { ids::id, 1 },
                                   { ids::name, "Pattern 1" },
                                   { ids::lengthSteps, 16 },
                                   { ids::genId, "" },
                                   // What the notes hashed to when the compiler wrote them.
                                   // Recompiling hashes them again: equal means nobody has touched
                                   // this pattern and it can be replaced, different means somebody
                                   // has and it must not be. Without it a recompile is a choice
                                   // between losing hand edits and never updating anything.
                                   { ids::genHash, "" } },
                                 { { "notes", &noteSpec(), true } } };
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
        { { ids::step, 0.0 },
          // 0..1 within the target's own range, so a point editor is uniform
          // whatever it is driving.
          { ids::value, 0.5 },
          // Bend between this point and the next: 0 is a straight line,
          // positive holds high longer, negative holds low longer.
          { ids::curve, 0.0 },
          // What that same segment IS. "curve" by default, which with a bend of
          // zero is the straight line every file written before shapes existed
          // already drew - so this is additive and needs no migration.
          { ids::shape, "curve" } },
        {}
    };
    return spec;
}

const NodeSpec& automationSpec()
{
    static const NodeSpec spec { ids::AUTOMATION,
                                 { { ids::id, 1 },
                                   { ids::name, "Automation" },
                                   { ids::scope, "channel" },
                                   { ids::targetId, 1 },
                                   { ids::slot, -1 },
                                   // The identifier, not "volume". This default names a property,
                                   // so spelling it out here is a second declaration that a rename
                                   // cannot follow - which would leave every new automation clip
                                   // pointing at a parameter that no longer exists, silently.
                                   { ids::param, ids::volume.toString() } },
                                 { { "points", &pointSpec(), true } } };
    return spec;
}

const NodeSpec& clipSpec()
{
    static const NodeSpec spec {
        ids::CLIP,
        // `kind` rather than replacing patternId with a generic refId: a
        // version 3 file has clips with no kind at all, and defaulting it to
        // "pattern" is what makes those load unchanged.
        { { ids::kind, "pattern" },
          { ids::patternId, 1 },
          { ids::automationId, 1 },
          // Which channel an "audio" clip plays, alongside the pattern and
          // automation references. Only the one matching `kind` is meaningful.
          { ids::channelId, 1 },
          { ids::startBar, 0 },
          { ids::lengthBars, 1 },
          { ids::genId, "" } },
        {}
    };
    return spec;
}

const NodeSpec& playlistTrackSpec()
{
    static const NodeSpec spec {
        ids::PLAYLIST_TRACK,
        { { ids::name, "Track" },
          { ids::mute, false },
          // How loud this LANE is. Not a bus - see the engine's note on
          // ClipSnapshot::trackGain - but a scale on what its clips trigger,
          // which is the one thing a lane genuinely owns. A file written before
          // version 16 has no such property and defaults to 1.0, which is what
          // every lane did when it could not carry one.
          { ids::gain, 1.0 },
          // Empty means inherit: the lane takes the colour of its POSITION,
          // which is what it did before it could carry one of its own.
          { ids::colour, "" },
          { ids::genId, "" } },
        { { "clips", &clipSpec(), true } }
    };
    return spec;
}

const NodeSpec& playlistSpec()
{
    static const NodeSpec spec { ids::PLAYLIST, {}, { { "tracks", &playlistTrackSpec(), true } } };
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
    static const NodeSpec spec { ids::MIXER_TRACK,
                                 { { ids::id, 1 },
                                   { ids::name, "Insert" },
                                   { ids::gain, 0.8 },
                                   { ids::pan, 0.0 },
                                   { ids::mute, false },
                                   // Empty means inherit: the strip takes the colours of the
                                   // channels routed into it, which is what it did before.
                                   { ids::colour, "" } },
                                 { { "effects", &effectSpec(), true } } };
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
    static const NodeSpec spec { ids::LINE, { { ids::text, "" } }, {} };
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
    static const NodeSpec spec { ids::SCORE,
                                 // The source's own file name, when it came from one. Diagnostics
                                 // are reported against a name, and "untitled.score:12" is worse
                                 // than the name the user knows it by.
                                 { { ids::name, "" } },
                                 { { "lines", &lineSpec(), true } } };
    return spec;
}

const NodeSpec& mixerSpec()
{
    static const NodeSpec spec { ids::MIXER,
                                 {},
                                 { { "master", &masterSpec(), false },
                                   { "tracks", &mixerTrackSpec(), true } } };
    return spec;
}

} // namespace

const NodeSpec& projectSpec()
{
    static const NodeSpec spec {
        ids::PROJECT,
        { { ids::formatVersion, kFormatVersion },
          { ids::name, "Untitled" },
          { ids::tempoBpm, 128.0 },
          { ids::stepsPerBeat, 4 },
          { ids::beatsPerBar, 4 },
          { ids::beatUnit, 4 },
          { ids::barsInSong, 16 } },
        { { "channels", &channelSpec(), true },
          { "patterns", &patternSpec(), true },
          { "automations", &automationSpec(), true },
          { "playlist", &playlistSpec(), false },
          { "mixer", &mixerSpec(), false },
          // Last, and permanently so: canonicalTree materialises children in
          // this order and isEquivalentTo compares them in order, so moving
          // this entry would make every committed project unequal to itself.
          { "score", &scoreSpec(), false } }
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

} // namespace dew
