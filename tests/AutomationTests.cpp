// The shape of a curve, and what its points may do.
//
// Split out of AutomationTests.cpp. Its tags divide it only three ways at the
// edges, so these are its subjects. The fixture is AutomationHarness.h.

#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "io/OfflineRenderer.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

#include "AutomationHarness.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("a curve reads back the shape it was drawn as", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);
    REQUIRE (automation.isValid());

    // A fresh automation is a line, not an empty box.
    REQUIRE (ProjectEdits::automationValueAt (automation, 0.0) > 0.0);

    // A ramp from 0 to 1 over 32 steps.
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 0.5 }, { 32.0, 1.0 } }, &undo);

    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 0.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.25, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 32.0), WithinAbs (1.0, 1e-9));

    // Held flat outside the points, rather than extrapolating off the end.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, -50.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 500.0), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("a point cannot be dragged past its neighbours", "[automation]")
{
    // The old name said "points stay in step order HOWEVER they are dragged",
    // which described the mechanism: the point went wherever it was dragged and
    // the tree was re-sorted behind it. That shuffled the children under the
    // point being dragged and put a moveChild on the undo stack for every frame.
    // The rule now is that the drag stops.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);

    auto a = ProjectEdits::addAutomationPoint (automation, 4.0, 0.2, &undo);
    ProjectEdits::addAutomationPoint (automation, 8.0, 0.8, &undo);

    const auto indexBefore = automation.indexOf (a);

    // Drag the earlier point far past the later one.
    ProjectEdits::moveAutomationPoint (automation, a, 20.0, 0.2, &undo);

    // It moved as far as it was allowed and no further.
    REQUIRE ((double) a[ids::step] > 4.0);
    REQUIRE_THAT ((double) a[ids::step], WithinAbs (8.0 - ProjectEdits::minPointGap, 1e-9));

    // And nothing was reordered - which is what removing the re-sort buys, and
    // what nothing else asserts.
    REQUIRE (automation.indexOf (a) == indexBefore);

    double previous = -1.0;

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
        {
            REQUIRE ((double) point[ids::step] >= previous);
            previous = (double) point[ids::step];
        }

    // Backwards too, and the first point still stops at zero rather than going
    // negative.
    ProjectEdits::moveAutomationPoint (automation, a, -50.0, 0.2, &undo);
    REQUIRE_THAT ((double) a[ids::step], WithinAbs (ProjectEdits::minPointGap, 1e-9));
}

TEST_CASE ("two points cannot share a step", "[automation]")
{
    // A step with two values has no defined value there.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);

    const auto countPoints = [&automation]
    {
        int n = 0;

        for (const auto& child : automation)
            if (child.hasType (ids::POINT))
                ++n;

        return n;
    };

    const auto before = countPoints();

    ProjectEdits::addAutomationPoint (automation, 12.0, 0.3, &undo);
    REQUIRE (countPoints() == before + 1);

    ProjectEdits::addAutomationPoint (automation, 12.0, 0.9, &undo);
    REQUIRE (countPoints() == before + 1);
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 12.0), WithinAbs (0.9, 1e-9));
}

TEST_CASE ("a curve keeps at least two points", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);

    juce::Array<juce::ValueTree> points;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            points.add (child);

    REQUIRE (points.size() == 2);

    ProjectEdits::removeAutomationPoint (automation, points.getFirst(), &undo);

    int remaining = 0;

    for (const auto& child : automation)
        if (child.hasType (ids::POINT))
            ++remaining;

    REQUIRE (remaining == 2);
}

TEST_CASE ("a discrete curve never lands between two states", "[automation]")
{
    // What makes a stepped target one model with a continuous one rather than a
    // special case: the SNAP is in the mapping every consumer already calls, so
    // a curve over a filter mode is a curve over the mode index and cannot ask
    // for a mode that does not exist.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto filter = ProjectEdits::addEffect (project, channel, "filter", &undo);
    REQUIRE (filter.isValid());

    const auto* spec = findParamSpec (AutomationScope::channelEffect, "filter", ids::filterMode);
    REQUIRE (spec != nullptr);
    REQUIRE (spec->automatable);
    REQUIRE (spec->numDiscreteValues() == 3);

    for (int i = 0; i <= 200; ++i)
    {
        const auto normalised = (double) i / 200.0;
        const auto value = automationValueFor (*spec, normalised);

        INFO ("at " << normalised << " the mode index is " << value);
        REQUIRE ((juce::approximatelyEqual (value, 0.0) || juce::approximatelyEqual (value, 1.0)
                  || juce::approximatelyEqual (value, 2.0)));
    }

    // Equal-width buckets, so the ends and the middle are the three modes.
    REQUIRE_THAT (automationValueFor (*spec, 0.0), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (automationValueFor (*spec, 0.5), WithinAbs (1.0, 1e-9));
    REQUIRE_THAT (automationValueFor (*spec, 1.0), WithinAbs (2.0, 1e-9));

    // And a continuous neighbour in the same table is untouched by the snap.
    const auto* cutoff = findParamSpec (AutomationScope::channelEffect, "filter", ids::cutoff);
    REQUIRE (cutoff != nullptr);
    REQUIRE (automationValueFor (*cutoff, 0.5) > cutoff->minimum);
    REQUIRE (automationValueFor (*cutoff, 0.5) < cutoff->maximum);
}

TEST_CASE ("a fresh curve over a discrete target is stepped", "[automation]")
{
    // A ramp between two states of a toggle is a shape nobody meant to draw, so
    // a bypass lane looks like one the moment it exists rather than after a trip
    // to the shape menu.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto continuous = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);

    for (const auto& point : ProjectEdits::sortedAutomationPoints (continuous))
        REQUIRE (point[ids::shape].toString() == "curve");

    auto stepped = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Off"),
                                                &undo);

    for (const auto& point : ProjectEdits::sortedAutomationPoints (stepped))
        REQUIRE (point[ids::shape].toString() == "step");
}

TEST_CASE ("frequency parameters sweep by ear, not by hertz", "[automation]")
{
    // A cutoff mapped linearly spends four fifths of a drawn curve above 3kHz.
    auto project = ProjectFactory::createDefault();

    const auto cutoffSpec = findParamSpec (AutomationScope::channelEffect, "filter", ids::cutoff);
    REQUIRE (cutoffSpec != nullptr);
    REQUIRE ((cutoffSpec->curve == ParamCurve::logarithmic));

    // Halfway up the curve is the geometric middle of the range, which is
    // roughly where a listener would put "halfway".
    const auto middle = automationValueFor (*cutoffSpec, 0.5);
    INFO ("midpoint " << middle);
    REQUIRE (middle > 400.0);
    REQUIRE (middle < 1200.0);

    // The ends still land exactly on the range.
    REQUIRE_THAT (automationValueFor (*cutoffSpec, 0.0), WithinAbs (cutoffSpec->minimum, 1e-6));
    REQUIRE_THAT (automationValueFor (*cutoffSpec, 1.0), WithinAbs (cutoffSpec->maximum, 1e-3));

    // Gain-like parameters stay linear: half volume means half.
    const auto volumeSpec = findParamSpec (AutomationScope::channel, "", ids::volume);
    REQUIRE (volumeSpec != nullptr);
    REQUIRE (volumeSpec->curve == ParamCurve::linear);
    REQUIRE_THAT (automationValueFor (*volumeSpec, 0.5), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("a stepped segment holds its left value until the next point", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.2 }, { 16.0, 0.8 } }, &undo);

    auto first = ProjectEdits::sortedAutomationPoints (automation).getFirst();
    REQUIRE (first.isValid());

    // As a curve with no bend it is a ramp: halfway across is halfway between.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));

    ProjectEdits::setPointShape (first, SegmentShape::step, &undo);

    // Stepped, every step of the segment reads the LEFT value - right up to but
    // not including the next point, where it jumps.
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 0.0), WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 15.9), WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 16.0), WithinAbs (0.8, 1e-9));

    // A step IGNORES the bend rather than losing it, so the shape is reversible.
    ProjectEdits::setPointCurve (first, 0.6, &undo);
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.2, 1e-9));

    ProjectEdits::setPointShape (first, SegmentShape::curve, &undo);
    REQUIRE_THAT ((double) first[ids::curve], WithinAbs (0.6, 1e-9));

    // And "Line" is that same shape with the bend flattened, in one write.
    ProjectEdits::setPointStraight (first, &undo);
    REQUIRE_THAT ((double) first[ids::curve], WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("a point added to a staircase does not put a ramp in it", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 1.0 } }, &undo);

    ProjectEdits::setPointShape (ProjectEdits::sortedAutomationPoints (automation).getFirst(),
                                 SegmentShape::step, &undo);

    auto added = ProjectEdits::addAutomationPoint (automation, 8.0, 0.5, &undo);
    REQUIRE (added.isValid());
    REQUIRE (added[ids::shape].toString() == "step");
}

TEST_CASE ("the editor and the engine agree about every point of a curve", "[automation]")
{
    // The two evaluators were hand-copied bodies of the same arithmetic in two
    // layers - ProjectEdits for the editor and the tests, AutomationSnapshot for
    // the audio thread - and nothing compared them. This is what would have
    // caught them drifting, and it is what keeps a segment shape from being
    // added to one and not the other.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.1 }, { 9.0, 0.85 }, { 20.0, 0.4 }, { 33.0, 1.0 } }, &undo);

    // A bend on the second segment, so the comparison covers the branch a
    // straight line does not reach.
    for (auto point : automation)
        if (point.hasType (ids::POINT) && juce::approximatelyEqual ((double) point[ids::step], 9.0))
            point.setProperty (ids::curve, 0.7, &undo);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (snapshot.automations.size() == 1);

    const auto& engine = snapshot.automations.front();
    REQUIRE (engine.spec != nullptr);

    // The engine reports the parameter's own units, so the editor's 0..1 goes
    // through the same mapping before they are compared - that mapping is not
    // what is under test here, the interpolation is.
    const auto& spec = *engine.spec;

    for (int i = 0; i <= 200; ++i)
    {
        const auto step = -5.0 + 45.0 * (double) i / 200.0;

        INFO ("step " << step);
        REQUIRE_THAT ((double) engine.valueAt (step),
                      WithinAbs (automationValueFor (
                                     spec, ProjectEdits::automationValueAt (automation, step)),
                                 1e-5));
    }
}

TEST_CASE ("a soundfont channel's offsets reach the engine as their own scope", "[automation]")
{
    // The scope did not exist, so the six knobs on the soundfont face offered a
    // reset and nothing else. What matters here is that a curve over one is
    // RESOLVED rather than dropped: buildSnapshot drops an automation whose
    // param is `none`, which is how a target nobody taught the reader about
    // becomes a line drawn on screen and heard by nothing.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = ProjectEdits::addSoundFontChannel (project, "Font", &undo);
    REQUIRE (channel.isValid());

    const auto target = targetNamed (project, "Font > Soundfont > Pitch");
    REQUIRE (target.scope == AutomationScope::channelSoundFont);
    REQUIRE (target.targetId == (int) channel[ids::id]);

    auto automation = ProjectEdits::addAutomation (project, target, &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 1.0 } }, &undo);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    INFO (warnings.joinIntoString ("\n"));
    REQUIRE (snapshot.automations.size() == 1);

    const auto& resolved = snapshot.automations.front();
    CHECK (resolved.scope == AutomationScope::channelSoundFont);
    CHECK (resolved.param == AutomationParam::sfTranspose);
    CHECK (resolved.targetIndex >= 0);
    REQUIRE (resolved.spec != nullptr);

    // The catalog's range, not a second copy of it: -24..24 semitones.
    CHECK (juce::exactlyEqual (resolved.spec->minimum, -24.0));
    CHECK (juce::exactlyEqual (resolved.spec->maximum, 24.0));
}

TEST_CASE ("an envelope curve reaches the engine, and only on a synth channel", "[automation]")
{
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (
        project, targetNamed (project, "Kick > Envelope > Attack"), &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 16.0, 1.0 } }, &undo);

    juce::StringArray warnings;
    REQUIRE (buildSnapshot (project, &warnings).automations.size() == 1);

    // And the gate that makes the inert nodes safe. Every channel carries an
    // AMP and a SOUNDFONT whichever kind it is, to keep the canonical tree one
    // shape - so a curve saved against a channel that has since been switched
    // to another source must be dropped rather than applied to settings the
    // engine never reads.
    auto channel = project.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    REQUIRE (ProjectEdits::setInstrumentType (channel, InstrumentType::audio, &undo));

    warnings.clear();
    const auto after = buildSnapshot (project, &warnings);

    // Dropped means an inert entry and a warning, not a missing one: a clip
    // refers to an automation by INDEX, so the row has to stay where it was.
    REQUIRE (after.automations.size() == 1);
    CHECK (after.automations.front().param == AutomationParam::none);
    CHECK_FALSE (warnings.isEmpty());
}
