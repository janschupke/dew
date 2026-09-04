// What the picker offers, and what a node can be asked to automate.
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

TEST_CASE ("every target the picker offers is one a control could ask for", "[automation]")
{
    // Both directions of one fact. The owner-node -> (scope, targetId, slot)
    // mapping used to live only inside the picker's own loop, so a knob had
    // nowhere to ask what it drove - which is why automation was reachable from
    // one button and not from the control itself. The picker is now a walk over
    // automationTargetFor, and this is what keeps the claim honest.
    auto project = dew::testing::fixtureProject();

    const auto offered = availableAutomationTargets (project);
    REQUIRE (offered.size() > 10);

    /** The node a target names, found the long way round - so this test does not
        share the walk it is checking. */
    const auto nodeFor = [&project] (const AutomationTarget& target) -> juce::ValueTree
    {
        const auto nth = [] (const juce::ValueTree& parent, const juce::Identifier& type, int index)
        {
            int i = 0;

            for (const auto& child : parent)
                if (child.hasType (type) && i++ == index)
                    return child;

            return juce::ValueTree();
        };

        const auto mixer = project.getChildWithName (ids::MIXER);

        if (target.scope == AutomationScope::master)
            return mixer.getChildWithName (ids::MASTER);

        // The arrangement itself, which is the project's own root.
        if (target.scope == AutomationScope::project)
            return project;

        juce::ValueTree owner;

        for (const auto& child : target.scope == AutomationScope::mixerTrack
                                         || target.scope == AutomationScope::mixerEffect
                                     ? mixer
                                     : project)
            if (child.hasType (target.scope == AutomationScope::mixerTrack
                                       || target.scope == AutomationScope::mixerEffect
                                   ? ids::MIXER_TRACK
                                   : ids::CHANNEL)
                && (int) child[ids::id] == target.targetId)
                owner = child;

        switch (target.scope)
        {
            case AutomationScope::channel:
            case AutomationScope::mixerTrack: return owner;

            case AutomationScope::channelOsc:
                return nth (owner.getChildWithName (ids::INSTRUMENT), ids::OSC, target.slot);

            case AutomationScope::channelEffect:
            case AutomationScope::mixerEffect: return nth (owner, ids::EFFECT, target.slot);

            case AutomationScope::project:
            case AutomationScope::master: break;
        }

        return {};
    };

    for (const auto& target : offered)
    {
        INFO ("target " << target.displayName);

        const auto node = nodeFor (target);
        REQUIRE (node.isValid());

        const auto resolved = automationTargetFor (project, node, target.property);
        REQUIRE (resolved.has_value());

        CHECK (resolved->scope == target.scope);
        CHECK (resolved->targetId == target.targetId);
        CHECK (resolved->slot == target.slot);
        CHECK (resolved->property == target.property);
        CHECK (resolved->displayName == target.displayName);
        CHECK (resolved->spec == target.spec);
    }
}

TEST_CASE ("a node with nothing to automate resolves to nothing", "[automation]")
{
    // The negatives matter as much: a resolver that said yes to everything would
    // put a "create automation clip" item on every control in the application.
    auto project = ProjectFactory::createDefault();

    const auto channel = project.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());

    // A property that exists on the node but is not automatable.
    REQUIRE_FALSE (automationTargetFor (project, channel, ids::basePitch).has_value());
    REQUIRE_FALSE (automationTargetFor (project, channel, ids::name).has_value());

    // An envelope stage: not a quantity you move THROUGH a note.
    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);
    REQUIRE (amp.isValid());
    REQUIRE_FALSE (automationTargetFor (project, amp, ids::attack).has_value());

    // A pattern, and a playlist track - which carries no id to point at.
    REQUIRE_FALSE (
        automationTargetFor (project, project.getChildWithName (ids::PATTERN), ids::lengthSteps)
            .has_value());

    const auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    REQUIRE (track.isValid());
    REQUIRE_FALSE (automationTargetFor (project, track, ids::mute).has_value());

    // And an invalid node, which is what a control on a deleted channel hands in.
    REQUIRE_FALSE (automationTargetFor (project, {}, ids::volume).has_value());
}

TEST_CASE ("a classic oscillator slot offers nothing to automate", "[automation]")
{
    // A wave position on a slot that is not running a wavetable would be a
    // control that silently did nothing. The picker has always skipped it; the
    // resolver has to agree, or a right-click would offer what the picker does
    // not.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto osc = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    REQUIRE (osc.isValid());

    ProjectEdits::setProperty (osc, ids::mode, "classic", &undo, "Classic");
    REQUIRE_FALSE (automationTargetFor (project, osc, ids::wavePosition).has_value());

    ProjectEdits::setProperty (osc, ids::mode, "wavetable", &undo, "Wavetable");
    REQUIRE (automationTargetFor (project, osc, ids::wavePosition).has_value());
}

TEST_CASE ("the target list covers channels, effects, tracks and master", "[automation]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    ProjectEdits::addEffect (project, channel, "delay", &undo);

    juce::StringArray names;

    for (const auto& target : availableAutomationTargets (project))
        names.add (target.displayName);

    const auto name = channel[ids::name].toString();

    REQUIRE (names.contains (name + " > Volume"));
    REQUIRE (names.contains (name + " > Pan"));
    REQUIRE (names.contains (name + " > Delay > Time"));
    REQUIRE (names.contains (name + " > Delay > Feedback"));
    REQUIRE (names.contains ("Insert 1 > Gain"));
    REQUIRE (names.contains ("Master > Gain"));

    // Nothing that is not a continuous quantity: no names, no lengths, no ids.
    for (const auto& target : availableAutomationTargets (project))
    {
        REQUIRE (target.spec != nullptr);
        REQUIRE (target.spec->maximum > target.spec->minimum);
        REQUIRE (target.spec->automatable);
        REQUIRE (target.property != ids::name);
        REQUIRE (target.property != ids::id);
    }
}
