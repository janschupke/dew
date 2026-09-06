// Where a knob's needle points, and whether it agrees with anything else.
//
// Three functions mapped 0..1 onto a parameter's own units and no two of them
// were the same. ParamSpec::fromNormalised is documented as "what a KNOB
// reads"; automationValueFor is what the picker, the point editor, the painter
// and the engine call; and DewKnob handed JUCE a power law computed from the
// range's endpoints. The third agreed with the first two at exactly 0, a half
// and 1. At a quarter of the travel an attack knob read one millisecond where a
// curve drawn to the same height played six.
//
// And the NEEDLE agreed with none of them: it divided by the raw span,
// discarding the curve, so on an envelope knob it sat pinned at the bottom for
// nine tenths of the sweep and then raced while the value moved smoothly.
//
// Nothing anywhere pinned any of this - grep tests/ for `skew`,
// `NormalisableRange` or `proportionOfValue` before this file and there were no
// hits at all.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ParamSpec.h"
#include "ui/primitives/DewKnob.h"

using namespace dew;

namespace
{

const ParamSpec& specFor (const juce::Identifier& property)
{
    const auto* spec = instrumentParamSpec (property);
    REQUIRE (spec != nullptr);
    return *spec;
}

const std::vector<double> travel { 0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0 };

} // namespace

TEST_CASE ("a knob's needle points where its own drag put the value", "[ui][design][params]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Every shape of parameter a knob is built from, not only the one that was
    // reported: a cubic envelope time, a logarithmic rate, a linear proportion
    // and a bipolar one.
    for (const auto* property : { &ids::attack, &ids::decay, &ids::release, &ids::wavePositionRate,
                                  &ids::volume, &ids::pan, &ids::sustain })
    {
        const auto& spec = specFor (*property);

        DewKnob knob { spec };
        knob.setSize (tokens::size::knob, tokens::size::knobRow);

        for (const auto t : travel)
        {
            knob.setValue (spec.fromNormalised (t), juce::dontSendNotification);

            INFO (property->toString() << " at " << t << " travel");
            CHECK ((double) knob.getValueProportion() == Catch::Approx (t).margin (0.02));
        }
    }
}

TEST_CASE ("a knob and a drawn curve reach the same value", "[ui][design][params][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The disagreement, stated for every automatable instrument parameter that
    // is drawn as a knob rather than typed into a field. A discrete parameter
    // is excluded on purpose: automationValueFor snaps one to a bucket and a
    // knob deliberately does not, which is a difference the two are supposed to
    // have.
    auto continuous = 0;

    for (const auto* table :
         { &channelParamSpecs(), &ampParamSpecs(), &oscParamSpecs(), &sampleParamSpecs(),
           &soundFontParamSpecs(), &mixerTrackParamSpecs() })
    {
        for (const auto& spec : *table)
        {
            if (! spec.automatable || spec.control != ParamControl::knob || spec.isDiscrete())
                continue;

            ++continuous;

            for (const auto t : travel)
            {
                INFO (spec.property->toString() << " at " << t << " travel");
                CHECK (automationValueFor (spec, t) == Catch::Approx (spec.fromNormalised (t)));
            }
        }
    }

    // Control case: a sweep that matched nothing would agree with everything.
    INFO ("continuous automatable knobs: " << continuous);
    REQUIRE (continuous > 3);
}

TEST_CASE ("an envelope time starts at zero and gives the short end the travel",
           "[catalog][params]")
{
    // The complaint, as numbers. Logarithmic could not reach zero, so the range
    // began at half a millisecond and spent half the knob below seventy
    // milliseconds; a cube puts the middle at an eighth of the range and still
    // leaves the short end most of the sweep.
    for (const auto* property : { &ids::attack, &ids::decay })
    {
        const auto& spec = specFor (*property);

        CHECK (spec.curve == ParamCurve::cubic);
        CHECK (spec.minimum == Catch::Approx (0.0));
        CHECK (spec.fromNormalised (0.0) == Catch::Approx (0.0));
        CHECK (spec.fromNormalised (0.5) == Catch::Approx (1.25));
        CHECK (spec.fromNormalised (1.0) == Catch::Approx (10.0));
    }

    // Release keeps a floor: zero attack and zero decay are instants, and zero
    // release is a click on every note-off.
    const auto& release = specFor (ids::release);
    CHECK (release.curve == ParamCurve::cubic);
    CHECK (release.minimum > 0.0);

    // Round trip, which is what a control needs to show where it sits.
    for (const auto t : travel)
        CHECK (release.toNormalised (release.fromNormalised (t)) == Catch::Approx (t));
}
