// What the document keeps of an automation, across save and load.
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

TEST_CASE ("an automation pointing at something deleted is dropped with a warning", "[automation]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto channel = project.getChildWithName (ids::CHANNEL);
    const auto name = channel[ids::name].toString();

    auto automation = ProjectEdits::addAutomation (
        project, targetNamed (project, name + " > Volume"), &undo);
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0 * 16, 2 * 16, &undo);

    juce::StringArray warnings;
    REQUIRE (buildSnapshot (project, &warnings).anyAutomation);
    REQUIRE (warnings.isEmpty());

    ProjectEdits::removeChannel (project, channel, &undo);

    warnings.clear();
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (! warnings.isEmpty());
    REQUIRE (warnings.joinIntoString (" ").contains ("no longer exists"));
    REQUIRE (! snapshot.anyAutomation);
}

TEST_CASE ("removing an automation removes the clips that used it", "[automation]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);

    const auto countClips = [&track]
    {
        int n = 0;

        for (const auto& clip : track)
            if (clip.hasType (ids::CLIP))
                ++n;

        return n;
    };

    const auto before = countClips();
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 0 * 16, 2 * 16, &undo);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 4 * 16, 2 * 16, &undo);
    REQUIRE (countClips() == before + 2);

    undo.beginNewTransaction ("Remove automation");
    REQUIRE (ProjectEdits::removeAutomation (project, automation, &undo));
    REQUIRE (countClips() == before);

    // And it is one undo step, like every other cascading removal.
    REQUIRE (undo.undo());
    REQUIRE (countClips() == before + 2);
}

TEST_CASE ("automation survives save and load", "[automation][schema]")
{
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Master > Gain"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.1 }, { 24.0, 0.9 } }, &undo);

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    ProjectEdits::addAutomationClip (track, (int) automation[ids::id], 1 * 16, 3 * 16, &undo);

    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());
    REQUIRE (loaded.tree.isEquivalentTo (project));

    const auto reloaded = ProjectEdits::findAutomation (loaded.tree, (int) automation[ids::id]);
    REQUIRE (reloaded.isValid());
    REQUIRE_THAT (ProjectEdits::automationValueAt (reloaded, 12.0),
                  WithinAbs (ProjectEdits::automationValueAt (automation, 12.0), 1e-9));
}

TEST_CASE ("a file from before shapes loads as the line it drew", "[automation][schema]")
{
    // The additive-default claim, asserted rather than argued: a point with no
    // `shape` key is a curve with whatever bend it had, which for every file
    // written before shapes existed is a bend of zero - a straight line.
    const juce::String older = R"({
      "format": "dew-project",
      "formatVersion": 10,
      "name": "Older project",
      "tempoBpm": 120.0, "stepsPerBeat": 4, "beatsPerBar": 4, "beatUnit": 4, "barsInSong": 4,
      "channels": [ { "id": 1, "name": "Kick", "mixerTrackId": 1 } ],
      "patterns": [ { "id": 1, "name": "Pattern 1", "lengthSteps": 16 } ],
      "automations": [ { "id": 1, "name": "Kick > Volume", "scope": "channel",
                         "targetId": 1, "slot": -1, "param": "volume",
                         "points": [ { "step": 0.0, "value": 0.0 },
                                     { "step": 16.0, "value": 1.0 } ] } ],
      "playlist": { "tracks": [ { "name": "Track 1" } ] },
      "mixer": { "master": { "gain": 1.0 }, "tracks": [ { "id": 1, "name": "Insert 1" } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (older);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto automation = ProjectEdits::findAutomation (loaded.tree, 1);
    REQUIRE (automation.isValid());

    for (const auto& point : automation)
        if (point.hasType (ids::POINT))
            REQUIRE (point[ids::shape].toString() == "curve");

    REQUIRE_THAT (ProjectEdits::automationValueAt (automation, 8.0), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("a point dragged between two steps survives save and load", "[automation][schema]")
{
    // The test above round-trips a curve whose points sit on whole steps, which
    // is the one case the bug could not reach: `step` was declared an int in
    // pointSpec, and coerceToTypeOf drives its conversion off the runtime type
    // of the declared default - so every point a DRAG produced was truncated
    // back to the last whole step on the way out, silently, and the curve
    // reloaded as a shape nobody drew.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto automation = ProjectEdits::addAutomation (project, targetNamed (project, "Kick > Volume"),
                                                   &undo);
    setCurve (automation, { { 0.0, 0.0 }, { 6.5, 0.5 }, { 13.25, 1.0 } }, &undo);

    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::findAutomation (loaded.tree, (int) automation[ids::id]);
    REQUIRE (reloaded.isValid());

    juce::Array<double> steps;

    for (const auto& point : reloaded)
        if (point.hasType (ids::POINT))
            steps.add ((double) point[ids::step]);

    REQUIRE (steps.size() == 3);
    REQUIRE_THAT (steps[1], WithinAbs (6.5, 1e-9));
    REQUIRE_THAT (steps[2], WithinAbs (13.25, 1e-9));
}
