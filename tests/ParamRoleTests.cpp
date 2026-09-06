// What a parameter DOES, and the colour that says so.
//
// Three claims are made elsewhere and are only claims until something checks
// them: that every parameter in the catalog has been classified, that the seven
// function colours read as one family rather than as seven unrelated hues, and
// that a control built from a spec actually paints in its parameter's colour.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AutomationTargets.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ModuleCatalog.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "ui/design/ParamPalette.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewKnob.h"

#include "PaintProbe.h"

using namespace dew;
using namespace dew::testing;

namespace
{

/** Every parameter the catalog declares, however it is reached.
    Five separate tables, and a role table that misses any one of them is a
    panel full of knobs painted in the generic accent with nothing saying so. */
std::vector<ParamSpec> everyCataloguedParam()
{
    std::vector<ParamSpec> all;

    const auto append = [&all] (const std::vector<ParamSpec>& specs)
    { all.insert (all.end(), specs.begin(), specs.end()); };

    append (commonEffectParams());

    for (const auto& descriptor : effectDescriptors())
        append ({ descriptor.params, descriptor.params + descriptor.numParams });

    for (const auto& descriptor : instrumentDescriptors())
        for (int g = 0; g < descriptor.numGroups; ++g)
        {
            const auto& group = descriptor.groups[(size_t) g];
            append ({ group.params, group.params + group.numParams });
        }

    append (mixerTrackParamSpecs());
    append (projectParamSpecs());

    return all;
}

} // namespace

TEST_CASE ("every catalogued parameter has a declared function role", "[design][model][role]")
{
    const auto all = everyCataloguedParam();

    // Control case: a walk that found nothing would pass this test forever.
    REQUIRE (all.size() > 40u);

    juce::StringArray unclassified;

    for (const auto& spec : all)
        if (! declaredRoleOf (*spec.property).has_value())
            unclassified.addIfNotAlreadyThere (spec.property->toString());

    // By NAME, so a parameter added without a role says which one it is rather
    // than leaving somebody to diff two tables.
    INFO ("parameters with no declared function role:\n" << unclassified.joinIntoString ("\n"));
    CHECK (unclassified.isEmpty());
}

TEST_CASE ("the role table declares nothing the catalog does not have", "[design][model][role]")
{
    // The other direction. A row for a property that was renamed away never
    // fires again, and nothing else in the build would ever mention it.
    const auto all = everyCataloguedParam();

    juce::StringArray catalogued;

    for (const auto& spec : all)
        catalogued.addIfNotAlreadyThere (spec.property->toString());

    juce::StringArray stale;

    for (const auto& declared : declaredRoleNames())
        if (! catalogued.contains (declared))
            stale.addIfNotAlreadyThere (declared);

    INFO ("role rows for parameters the catalog no longer has:\n" << stale.joinIntoString ("\n"));
    CHECK (stale.isEmpty());
}

TEST_CASE ("no property is declared with two roles", "[design][model][role]")
{
    // The assumption the lookup rests on: a role is keyed on the IDENTIFIER, so
    // `gain` has to mean the same kind of thing on an oscillator and on a
    // fader. It does today. A second row would make declaredRoleOf return
    // whichever came first, silently.
    const auto names = declaredRoleNames();

    juce::StringArray seen, duplicated;

    for (const auto& name : names)
        if (! seen.addIfNotAlreadyThere (name))
            duplicated.addIfNotAlreadyThere (name);

    INFO ("properties with more than one role row:\n" << duplicated.joinIntoString ("\n"));
    CHECK (duplicated.isEmpty());
}

TEST_CASE ("the function palette is one family", "[design][tokens][role]")
{
    using namespace tokens;

    // The family is a set of COLOURS, not a set of roles, because the two
    // stopped being the same list. `generic` was never a function - it is the
    // accent everything multipurpose already uses - and `level` joined it: a
    // volume or a gain is the control a person reaches for most and the app's
    // own colour is what says so.
    //
    // funcLevel is still in the palette and still painted, on the mixer's
    // meter, so it is still held to every rule the rest of the family is: a
    // level as a SIGNAL is a function, a level as a CONTROL is the primary.
    const struct
    {
        const char* name;
        juce::Colour value;
    } functions[] {
        { "Level", colour::funcLevel },
        { "Stereo", palette::forRole (ParamRole::stereo) },
        { "Tone", palette::forRole (ParamRole::tone) },
        { "Time", palette::forRole (ParamRole::time) },
        { "Space", palette::forRole (ParamRole::space) },
        { "Modulation", palette::forRole (ParamRole::modulation) },
        { "Pitch", palette::forRole (ParamRole::pitch) },
    };

    CHECK (palette::forRole (ParamRole::generic) == colour::accent);
    CHECK (palette::forRole (ParamRole::level) == colour::accent);

    /** Degrees apart on the wheel, the short way round. */
    const auto hueGap = [] (juce::Colour a, juce::Colour b)
    {
        const auto degrees = std::abs (a.getHue() - b.getHue()) * 360.0f;
        return juce::jmin (degrees, 360.0f - degrees);
    };

    for (const auto& f : functions)
    {
        const auto c = f.value;

        // One family: a palette whose members had different chroma would read
        // as some colours and some highlights rather than as a set.
        INFO ("func" << f.name << " saturation " << c.getSaturation());
        CHECK (c.getSaturation() > 0.30f);
        CHECK (c.getSaturation() < 0.40f);

        INFO ("func" << f.name << " brightness " << c.getBrightness());
        CHECK (c.getBrightness() > 0.70f);
        CHECK (c.getBrightness() < 0.90f);

        // Quieter than identity, against EVERY identity colour rather than
        // against their average. A channel's colour and a function's colour
        // meet on the playlist and identity has to win there, so the ramp's
        // quietest entry is the bar - beating the average would still leave two
        // function colours louder than the aqua channel.
        for (int i = 0; i < entityColour::rampSize(); ++i)
        {
            INFO ("func" << f.name << " against channel " << i);
            CHECK (c.getSaturation() < colour::channelColour (i).getSaturation());
        }

        // Not competing with selection.
        INFO ("func" << f.name << " is " << hueGap (c, colour::accent) << " degrees from accent");
        CHECK (hueGap (c, colour::accent) > 30.0f);
    }

    for (const auto& a : functions)
        for (const auto& b : functions)
        {
            if (a.value == b.value)
                continue;

            // Told apart at the width of a knob's arc, which is the smallest
            // thing any of them is ever drawn as.
            INFO ("func" << a.name << " and func" << b.name << " are " << hueGap (a.value, b.value)
                         << " degrees apart");
            CHECK (hueGap (a.value, b.value) > 30.0f);
        }
}

TEST_CASE ("a knob built from a spec paints its parameter's function colour",
           "[design][primitives][role]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Cutoff is a tone control and volume is a level one. Both used to paint
    // the same accent arc, which is the whole reason for this change - and
    // volume has since been given it back on purpose, because the control a
    // person reaches for most is the one the app's own colour should mark. So
    // the claim here is no longer "never the accent" but "the colour its ROLE
    // maps to", which is what forRole says and what a knob has to obey.
    const auto renderKnob = [] (const ParamSpec& spec, double value)
    {
        DewKnob knob { spec };
        knob.setSize (tokens::size::knob, tokens::size::knobRow);
        knob.setVisible (true);
        knob.resized();
        knob.setValue (value, juce::dontSendNotification);

        return render (knob);
    };

    // Every function colour plus the accent these all used to be, so each knob
    // is asked which of the eight it is MOST drawn in rather than whether it
    // contains a colour at all - see strongestCoverage.
    const std::vector<juce::Colour> choices {
        tokens::colour::funcLevel, tokens::colour::funcStereo, tokens::colour::funcTone,
        tokens::colour::funcTime,  tokens::colour::funcSpace,  tokens::colour::funcModulation,
        tokens::colour::funcPitch, tokens::colour::accent,
    };

    // Scoped, because `time` and `space` name things in the global namespace and
    // -Wshadow is an error here.
    enum class Fn
    {
        level = 0,
        stereo,
        tone,
        time,
        space,
        modulation,
        pitch,
        accent
    };

    const auto strongest = [&choices] (const juce::Image& image)
    { return (Fn) strongestCoverage (image, choices); };

    const auto& volume = requireInstrumentParamSpec (ids::volume);
    const auto volumeImage = renderKnob (volume, 0.8);

    // A LEVEL is the accent, and funcLevel - which it used to be - must not be
    // on it at all: the two are close enough on the wheel that "contains some
    // accent" would pass while still painting green.
    CHECK (coverageOf (volumeImage, tokens::colour::accent) > 0.0f);
    CHECK (strongest (volumeImage) == Fn::accent);
    CHECK (juce::exactlyEqual (coverageOf (volumeImage, tokens::colour::funcLevel), 0.0f));

    const auto& pan = requireInstrumentParamSpec (ids::pan);
    const auto panImage = renderKnob (pan, -0.7);

    CHECK (strongest (panImage) == Fn::stereo);

    // Two knobs of the same shape and range that mean different things.
    const auto& attack = requireInstrumentParamSpec (ids::attack);
    const auto attackImage = renderKnob (attack, 2.0);

    CHECK (strongest (attackImage) == Fn::time);

    // And a knob whose role IS a function is never the accent, which is the
    // half of the original change that still stands: accent no longer means
    // "this is a value", it means level, selection and focus.
    CHECK (juce::exactlyEqual (coverageOf (attackImage, tokens::colour::accent), 0.0f));
}

TEST_CASE ("an automation node resolves back to the parameter it drives",
           "[automation][model][role]")
{
    // The resolution the playlist painter went without: it drew every clip in
    // one colour and passed `bipolar` as false, so a pan curve filled from the
    // bottom while the knob it drives fills from the centre.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    // The picker's own list, so a target this test builds and one a person
    // picks cannot be two different things.
    const auto targets = availableAutomationTargets (project);

    const auto targetDriving = [&targets] (const juce::Identifier& property)
    {
        std::optional<AutomationTarget> found;

        for (const auto& target : targets)
            if (target.property == property && ! found.has_value())
                found = target;

        return found;
    };

    // A pan: bipolar, and a stereo control.
    const auto panTarget = targetDriving (ids::pan);
    REQUIRE (panTarget.has_value());

    auto pan = ProjectEdits::addAutomation (project, *panTarget, &undo);
    const auto* panSpec = specForAutomation (project, pan);

    REQUIRE (panSpec != nullptr);
    CHECK (*panSpec->property == ids::pan);
    CHECK (panSpec->bipolar);
    CHECK (roleOf (*panSpec->property) == ParamRole::stereo);

    // A gain: not bipolar, and a level. The pair is the point - the painter
    // gets `bipolar` from here now, and these two disagree about it.
    const auto gainTarget = targetDriving (ids::gain);
    REQUIRE (gainTarget.has_value());

    auto gain = ProjectEdits::addAutomation (project, *gainTarget, &undo);
    const auto* gainSpec = specForAutomation (project, gain);

    REQUIRE (gainSpec != nullptr);
    CHECK (! gainSpec->bipolar);
    CHECK (roleOf (*gainSpec->property) == ParamRole::level);

    // A clip pointing at nothing resolves to nothing, rather than to whatever
    // the first table happens to hold.
    ProjectEdits::setProperty (gain, ids::param, "notAParameter", &undo,
                               TransactionName { "Point at nothing" });
    CHECK (specForAutomation (project, gain) == nullptr);
}
