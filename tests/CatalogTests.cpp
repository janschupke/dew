#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
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
