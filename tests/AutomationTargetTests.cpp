// What the picker offers, and what a node can be asked to automate.
//
// Split out of AutomationTests.cpp. Its tags divide it only three ways at the
// edges, so these are its subjects. The fixture is AutomationHarness.h.

#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/SnapshotReaders.h"
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

            case AutomationScope::channelAmp:
                return owner.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

            case AutomationScope::channelSoundFont: return owner.getChildWithName (ids::SOUNDFONT);

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

    // A recording's own settings. A fade and a transpose are set once for a
    // take rather than moved through it, and reversing a sample is a
    // discontinuity in a read pointer rather than a parameter change.
    const auto sample = channel.getChildWithName (ids::SAMPLE);
    REQUIRE (sample.isValid());
    REQUIRE_FALSE (automationTargetFor (project, sample, ids::fadeInMs).has_value());
    REQUIRE_FALSE (automationTargetFor (project, sample, ids::reverse).has_value());

    // A soundfont node under a channel that plays no soundfont. The scope
    // exists, but this channel's SOUNDFONT is one of the inert nodes every
    // channel carries to keep the canonical tree one shape.
    const auto soundFont = channel.getChildWithName (ids::SOUNDFONT);
    REQUIRE (soundFont.isValid());
    REQUIRE (automationTargetFor (project, soundFont, ids::tuneCents).has_value());

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

TEST_CASE ("a slot offers its generator's parameters and not another's", "[automation]")
{
    // A wave position on a slot that is not running a wavetable would be a
    // control that silently did nothing. The picker has always skipped it; the
    // resolver has to agree, or a right-click would offer what the picker does
    // not.
    //
    // What was wrong was the SHAPE of that rule. The whole scope was gated on
    // the mode, so a classic slot offered nothing at all - and its `gain`,
    // declared automatable in the catalog since the catalog was written, could
    // not be reached from anywhere.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    auto osc = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    REQUIRE (osc.isValid());

    ProjectEdits::setProperty (osc, ids::mode, "classic", &undo, TransactionName { "Classic" });
    CHECK_FALSE (automationTargetFor (project, osc, ids::wavePosition).has_value());

    // The slot's own, which a mode gate had no business refusing.
    CHECK (automationTargetFor (project, osc, ids::gain).has_value());

    ProjectEdits::setProperty (osc, ids::mode, "wavetable", &undo, TransactionName { "Wavetable" });
    CHECK (automationTargetFor (project, osc, ids::wavePosition).has_value());
    CHECK (automationTargetFor (project, osc, ids::gain).has_value());

    // And the picker agrees with the resolver, which is the pair the whole
    // table exists to keep honest.
    ProjectEdits::setProperty (osc, ids::mode, "classic", &undo, TransactionName { "Classic" });

    juce::StringArray offered;

    for (const auto& target : availableAutomationTargets (project))
        if (target.scope == AutomationScope::channelOsc)
            offered.add (target.property.toString());

    INFO ("offered on a classic slot:\n" << offered.joinIntoString ("\n"));
    CHECK (offered.contains ("gain"));
    CHECK_FALSE (offered.contains ("wavePosition"));
}

TEST_CASE ("every parameter a slot offers is one the engine applies", "[automation][engine]")
{
    // Four of the five the catalog declares reached the snapshot and stopped
    // there: AudioEngineAutomation applied `position` and nothing else, so a
    // curve over an oscillator's gain, its position mod, its LFO rate or its
    // unison spread moved a line on screen and nothing in the sound.
    for (const auto* generator : { "classic", "wavetable" })
    {
        for (const auto& spec : oscParams (generator))
        {
            const auto param = snapshotRead::automationParamFromIdentifier (
                AutomationScope::channelOsc, *spec.property);

            INFO (generator << " > " << spec.property->toString());
            CHECK (param != AutomationParam::none);
        }
    }
}

TEST_CASE ("the target list covers channels, effects, tracks and master", "[automation]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    ProjectEdits::addEffect (project, channel, "delay", &undo);

    const auto targets = availableAutomationTargets (project);

    // Asked of the target's two halves rather than of the composed string.
    // Spelling "Kick > Delay > Time" here would be this test holding the
    // catalogue's English against itself, and it would fail the day somebody
    // corrected a parameter's name rather than the day the picker lost one.
    const auto offers = [&targets] (const juce::String& owner, StringId param)
    {
        for (const auto& target : targets)
            if (target.ownerName == owner && target.paramName == tr (param))
                return true;

        return false;
    };

    const auto within = [] (const juce::String& owner, StringId part)
    {
        return tr (StringId::automation_parameter,
                   Args {}.with ("owner", owner).with ("param", tr (part)));
    };

    const auto name = channel[ids::name].toString();

    REQUIRE (offers (name, StringId::param_volume_name));
    REQUIRE (offers (name, StringId::param_pan_name));
    REQUIRE (offers (within (name, StringId::effect_delay_name), StringId::param_delayMs_name));
    REQUIRE (offers (within (name, StringId::effect_delay_name), StringId::param_feedback_name));
    REQUIRE (offers ("Insert 1", StringId::param_gain_name));
    REQUIRE (offers (tr (StringId::automation_master), StringId::param_gain_name));

    juce::StringArray names;

    for (const auto& target : targets)
        names.add (target.displayName);

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
