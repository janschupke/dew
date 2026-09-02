#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/AutomationTargets.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"

using namespace dew;
using Catch::Approx;

namespace
{

const juce::Array<EffectType> allTypes { EffectType::filter, EffectType::reverb, EffectType::delay,
                                         EffectType::drive, EffectType::chorus, EffectType::eq };

} // namespace

TEST_CASE ("every effect type has a descriptor", "[catalog]")
{
    // The one thing an explicit table cannot get for free. Adding an enumerator
    // without a row here used to leave four things silently wrong at once: the
    // type read back as a filter, it had no automatable parameters, its card
    // drew no controls, and it was missing from the add menu.
    REQUIRE ((int) effectDescriptors().size() == kNumEffectTypes);
    REQUIRE (allTypes.size() == kNumEffectTypes);

    for (const auto type : allTypes)
    {
        const auto& descriptor = effectDescriptor (type);

        INFO ("effect type " << (int) type);
        REQUIRE (descriptor.type == type);
        REQUIRE (juce::String (descriptor.id).isNotEmpty());
        REQUIRE (juce::String (descriptor.displayName).isNotEmpty());
        REQUIRE (descriptor.numParams > 0);
    }
}

TEST_CASE ("an effect type round-trips through its stored id", "[catalog]")
{
    for (const auto type : allTypes)
    {
        const auto id = effectTypeToString (type);
        INFO ("id: " << id);

        const auto back = effectTypeFor (id);
        REQUIRE (back.has_value());
        REQUIRE (*back == type);
    }
}

TEST_CASE ("an unknown effect id is refused rather than guessed", "[catalog]")
{
    // It used to become a low-pass filter, with nothing said. A project written
    // by a newer dew played back wrong and silently.
    REQUIRE_FALSE (effectTypeFor ("bitcrusher").has_value());
    REQUIRE_FALSE (effectTypeFor ("").has_value());
    REQUIRE_FALSE (effectTypeFor ("Filter").has_value());   // ids are exact
}

TEST_CASE ("effect ids are unique", "[catalog]")
{
    juce::StringArray ids;

    for (const auto& descriptor : effectDescriptors())
        ids.add (descriptor.id);

    const auto before = ids.size();
    ids.removeDuplicates (false);

    REQUIRE (ids.size() == before);
}

TEST_CASE ("every declared parameter is in the schema, with the same default", "[catalog][schema]")
{
    // The schema is generated from the catalog, so this checks the generation
    // did what it says rather than that two tables agree. Built the way
    // production builds one, through ProjectEdits, so the test cannot pass on a
    // path nothing uses.
    auto project = ProjectFactory::createDefault();
    auto channel = project.getChild (0);
    REQUIRE (channel.hasType (ids::CHANNEL));

    const auto tree = ProjectEdits::addEffect (project, channel, "filter", nullptr);
    REQUIRE (tree.isValid());

    for (const auto type : allTypes)
    {
        for (const auto& param : effectParamsFor (type))
        {
            INFO ("parameter " << param.property->toString());

            REQUIRE (tree.hasProperty (*param.property));
            REQUIRE (tree[*param.property] == param.defaultVar());
        }
    }
}

TEST_CASE ("every parameter's default is inside its own range", "[catalog]")
{
    // A default outside its range is a control that jumps the moment it is
    // touched, and a file whose stored value the engine will not honour.
    for (const auto type : allTypes)
    {
        for (const auto& param : effectParamsFor (type))
        {
            if (param.control == ParamControl::choice)
                continue;

            INFO ("parameter " << param.property->toString());
            REQUIRE (param.clamp (param.defaultValue) == Approx (param.defaultValue));
        }
    }
}

TEST_CASE ("a parameter maps its whole range, exactly at the ends", "[catalog]")
{
    for (const auto type : allTypes)
    {
        for (const auto& param : effectParamsFor (type))
        {
            if (param.control == ParamControl::choice)
                continue;

            INFO ("parameter " << param.property->toString());

            // Exactly, not nearly: a curve drawn to the top must reach the top,
            // and "almost the maximum" on a resonant filter is audible.
            REQUIRE (juce::exactlyEqual (param.fromNormalised (0.0), param.minimum));
            REQUIRE (juce::exactlyEqual (param.fromNormalised (1.0), param.maximum));

            // And back again, through whichever curve it uses.
            for (const auto t : { 0.0, 0.25, 0.5, 0.75, 1.0 })
                REQUIRE (param.toNormalised (param.fromNormalised (t)) == Approx (t).margin (1e-9));
        }
    }
}

TEST_CASE ("a choice parameter declares its choices and a default among them", "[catalog]")
{
    auto seenAChoice = false;

    for (const auto type : allTypes)
    {
        for (const auto& param : effectParamsFor (type))
        {
            if (param.control != ParamControl::choice)
                continue;

            seenAChoice = true;
            INFO ("parameter " << param.property->toString());

            REQUIRE (param.numChoices > 0);
            REQUIRE (param.defaultText != nullptr);

            auto found = false;

            for (int i = 0; i < param.numChoices; ++i)
                if (juce::String (param.choices[i].id) == param.defaultText)
                    found = true;

            REQUIRE (found);
        }
    }

    // A control case: if nothing in the catalog is a choice any more, the loop
    // above proved nothing and should stop claiming to.
    REQUIRE (seenAChoice);
}

TEST_CASE ("the parameter block is wide enough for every effect", "[catalog]")
{
    // kMaxEffectParams sizes the array the audio thread reads. A type wider than
    // it would not fail to compile - it would read whatever followed in memory.
    for (const auto& descriptor : effectDescriptors())
    {
        INFO ("effect " << descriptor.id << " has " << descriptor.numParams << " parameters");
        REQUIRE (kNumCommonEffectParams + descriptor.numParams <= kMaxEffectParams);
    }
}

TEST_CASE ("every parameter has a place in its block, and mix is first", "[catalog]")
{
    for (const auto type : allTypes)
    {
        // Index 0 is mix for every type, which is what lets the host apply
        // dry/wet identically without asking the module where its mix lives.
        REQUIRE (effectParamIndex (type, ids::mix) == 0);

        juce::Array<int> seen;

        for (const auto& param : effectParamsFor (type))
        {
            const auto index = effectParamIndex (type, *param.property);

            INFO ("parameter " << param.property->toString());
            REQUIRE (index >= 0);
            REQUIRE (index < kMaxEffectParams);

            // No two parameters may share a slot, or one would silently drive
            // the other.
            REQUIRE_FALSE (seen.contains (index));
            seen.add (index);
        }
    }

    // A property this type does not have is refused rather than aliased onto
    // something it does have.
    REQUIRE (effectParamIndex (EffectType::reverb, ids::cutoff) == -1);
}

TEST_CASE ("every instrument parameter says the same thing to the file and to the engine",
           "[catalog][params]")
{
    // The drift this closes. Every one of these had its range written out
    // twice - once as an engine clamp and once as a knob - and in every case
    // the knob was the narrower, so the top of what the engine renders was
    // unreachable:
    //
    //     attack   engine 0.0005..10s   knob 0.0005..2s
    //     decay    engine 0.0005..10s   knob 0.0005..4s
    //     release  engine 0.002..10s    knob 0.002..4s
    //     octave   engine -4..+4        stepper -3..+3
    // Built from the SCHEMA, not from the demo: the comparison is against what
    // a fresh document declares, and the demo deliberately sets a kick to
    // pitch 36.
    const auto& channelNode = childSpecFor (projectSpec(), "channels");

    const auto& instrumentNode = childSpecFor (channelNode, "instrument");

    const auto channel = defaultTreeFor (channelNode);
    const auto amp = defaultTreeFor (childSpecFor (instrumentNode, "amp"));
    const auto osc = defaultTreeFor (childSpecFor (instrumentNode, "oscillators"));

    struct Case { const juce::ValueTree& node; const std::vector<ParamSpec>& specs; const char* what; };

    const Case cases[] {
        { channel, channelParamSpecs(), "channel" },
        { amp,     ampParamSpecs(),     "amp" },
        { osc,     oscParamSpecs(),     "oscillator" },
    };

    for (const auto& c : cases)
    {
        INFO ("the " << c.what << " table");
        REQUIRE (! c.specs.empty());
        REQUIRE (c.node.isValid());

        for (const auto& spec : c.specs)
        {
            INFO ("parameter " << spec.property->toString());

            // The catalog's default IS the schema's, so a node that has never
            // been written and one that was written with the default read the
            // same. They were separate tables before this.
            REQUIRE (c.node.hasProperty (*spec.property));
            CHECK ((double) c.node[*spec.property] == Catch::Approx ((double) spec.defaultVar()));

            // A range that does not contain its own default is a table entry
            // that clamps every fresh document on the first read.
            CHECK ((double) spec.defaultVar() >= spec.minimum);
            CHECK ((double) spec.defaultVar() <= spec.maximum);

            CHECK (spec.maximum > spec.minimum);
            CHECK (spec.interval > 0.0);

            // Clamping the default must be the default, or a document that
            // stores what the schema wrote comes back changed.
            CHECK (spec.clamp ((double) spec.defaultVar())
                   == Catch::Approx ((double) spec.defaultVar()));

            // A logarithmic parameter cannot start at zero: the mapping is a
            // ratio, and a ratio to nothing has no middle.
            if (spec.curve == ParamCurve::logarithmic)
                CHECK (spec.minimum > 0.0);
        }
    }

    // And every one is reachable by property, which is how both the engine and
    // the panel ask for it.
    CHECK (instrumentParamSpec (ids::attack) != nullptr);
    CHECK (instrumentParamSpec (ids::octave) != nullptr);
    CHECK (instrumentParamSpec (ids::volume) != nullptr);
    CHECK (instrumentParamSpec (ids::cutoff) == nullptr);   // an effect's, not an instrument's
}

TEST_CASE ("the envelope reaches as far as the engine renders", "[catalog][params]")
{
    // Stated as values rather than only as an invariant, because the point of
    // the change is that these particular numbers moved.
    CHECK (requireInstrumentParamSpec (ids::attack).maximum == Catch::Approx (10.0));
    CHECK (requireInstrumentParamSpec (ids::decay).maximum == Catch::Approx (10.0));
    CHECK (requireInstrumentParamSpec (ids::release).maximum == Catch::Approx (10.0));
    CHECK (requireInstrumentParamSpec (ids::octave).maximum == Catch::Approx (4.0));
    CHECK (requireInstrumentParamSpec (ids::octave).minimum == Catch::Approx (-4.0));

    // Detune is the one where the narrower control wins, and deliberately: the
    // engine clamps at twelve semitones, but that is what `octave` is for, and
    // a knob covering two octaves cannot be nudged by a cent.
    CHECK (requireInstrumentParamSpec (ids::detuneCents).maximum == Catch::Approx (100.0));
}

TEST_CASE ("automation reaches everything a control does", "[catalog][params]")
{
    // The three-way disagreement, stated as a test. A mixer fader offered
    // 0..1.5, the engine clamped at 2.0, and an automation curve mapped onto
    // 0..1 - so automating a fader swept two thirds of it and stopped, and
    // nothing anywhere said why.
    struct Case { AutomationScope scope; const juce::Identifier& property;
                  const std::vector<ParamSpec>& specs; };

    const Case cases[] {
        { AutomationScope::channel,    ids::volume,       channelParamSpecs() },
        { AutomationScope::channel,    ids::pan,          channelParamSpecs() },
        { AutomationScope::mixerTrack, ids::gain,         mixerTrackParamSpecs() },
        { AutomationScope::mixerTrack, ids::pan,          mixerTrackParamSpecs() },
        { AutomationScope::master,     ids::gain,         mixerTrackParamSpecs() },
        { AutomationScope::channelOsc, ids::wavePosition, oscParamSpecs() },
    };

    for (const auto& c : cases)
    {
        INFO ("automating " << c.property.toString());

        const auto* automation = findParamSpec (c.scope, {}, c.property);
        REQUIRE (automation != nullptr);

        const ParamSpec* declared = nullptr;

        for (const auto& spec : c.specs)
            if (*spec.property == c.property)
                declared = &spec;

        REQUIRE (declared != nullptr);

        CHECK (automation->minimum == Catch::Approx (declared->minimum));
        CHECK (automation->maximum == Catch::Approx (declared->maximum));
        CHECK (automation->bipolar == declared->bipolar);
        CHECK (automation->logarithmic == (declared->curve == ParamCurve::logarithmic));

        // The value a curve at full height asks for must survive the clamp the
        // engine puts it through - the assertion the mixer's gain failed.
        const auto atFullHeight = mapAutomationValue (*automation, 1.0);

        CHECK (declared->clamp (atFullHeight) == Catch::Approx (atFullHeight));
        CHECK (atFullHeight == Catch::Approx (declared->maximum));
    }
}
