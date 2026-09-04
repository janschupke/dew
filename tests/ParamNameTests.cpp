#include <catch2/catch_test_macros.hpp>

#include "i18n/Strings.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ParamNames.h"

using namespace dew;

TEST_CASE ("every catalogued parameter is named exactly once", "[catalog][i18n]")
{
    // ParamNames is a lookup on the property identifier rather than two fields
    // on ParamSpec, and that trade has one cost: nothing about a row in the
    // catalog makes a row here appear. A parameter added without one falls back
    // to param.unknown and paints a knob with "?", which is visible but only to
    // somebody who opens that panel.
    //
    // This is what makes it a build failure instead. The same shape as
    // ParamRoleTests, for the same reason.
    juce::StringArray unnamed;
    auto checked = 0;

    const auto check = [&] (const ParamSpec& spec)
    {
        ++checked;

        if (tr (paramNameOf (*spec.property)) == tr (StringId::param_unknown_name))
            unnamed.addIfNotAlreadyThere (spec.property->toString());
    };

    for (const auto type :
         { InstrumentType::synth, InstrumentType::audio, InstrumentType::soundfont })
    {
        const auto& descriptor = instrumentDescriptor (type);

        for (int g = 0; g < descriptor.numGroups; ++g)
            for (int p = 0; p < descriptor.groups[g].numParams; ++p)
                check (descriptor.groups[g].params[p]);
    }

    for (const auto& descriptor : effectDescriptors())
        for (int p = 0; p < descriptor.numParams; ++p)
            check (descriptor.params[p]);

    // Control case: a walk that reached no parameter would report none unnamed.
    INFO ("parameters walked: " << checked);
    REQUIRE (checked > 60);

    INFO ("parameters with no row in ParamNames.cpp:\n" << unnamed.joinIntoString ("\n"));
    CHECK (unnamed.isEmpty());
}

TEST_CASE ("a parameter's name and caption read as they did", "[catalog][i18n]")
{
    // The names moved out of the catalog rows and into the catalogue, and this
    // pass ships English only - so a sample of them has to come back spelled
    // exactly as the rows spelled them. Both halves, because they are separate
    // keys and swapping the two would pass any test that checked one.
    CHECK (tr (paramNameOf (ids::cutoff)) == "Cutoff");
    CHECK (tr (paramCaptionOf (ids::cutoff)) == "CUTOFF");

    CHECK (tr (paramNameOf (ids::unisonDetune)) == "Spread");
    CHECK (tr (paramCaptionOf (ids::unisonDetune)) == "SPREAD");

    // Two identifiers that share a spelling stay two keys, because a locale is
    // free to tell an amplitude envelope's attack from a filter envelope's.
    CHECK (tr (paramNameOf (ids::attack)) == "Attack");
    CHECK (tr (paramNameOf (ids::attackScale)) == "Attack");
    CHECK (paramNameOf (ids::attack) != paramNameOf (ids::attackScale));
}
