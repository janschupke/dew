// Compiling again, over what the last compile wrote.
//
// Split out of ScoreBakeTests.cpp, along the Catch2 tags it already
// carried. The fixture is ScoreBakeHarness.h.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

#include "io/OfflineRenderer.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "model/ScoreBake.h"

#include "FixtureProject.h"
#include "ScoreBakeHarness.h"

using namespace dew;
using namespace dew::testing;

TEST_CASE ("compiling the same score twice leaves the same document", "[score][bake][recompile]")
{
    // The property that makes a .dew worth storing a score in: recompiling is
    // an update, not an accumulation. Pattern ids hold still too, so a clip a
    // user dragged somewhere else still points at what it pointed at.
    const auto score = compileOrFail (exampleSource());

    BakeReport first;
    auto project = ScoreBake::toNewProject (score, first);
    const auto afterFirst = project.createCopy();

    BakeReport second;
    second = ScoreBake::into (project, score, nullptr);

    REQUIRE (second.warnings.isEmpty());
    REQUIRE (second.patternsWritten == first.patternsWritten);
    REQUIRE (second.patternsKept == 0);
    REQUIRE (second.patternsRemoved == 0);
    REQUIRE (second.channelsCreated == 0); // found again, not made again

    REQUIRE (project.isEquivalentTo (afterFirst));
}

TEST_CASE ("a generated pattern edited by hand is kept, and said so", "[score][bake][recompile]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);
    ScoreBake::into (project, score, nullptr);

    auto generated = generatedPattern (project);
    REQUIRE (generated.isValid());

    // One note moved in the piano roll - the smallest possible edit.
    auto note = generated.getChildWithName (ids::NOTE);
    REQUIRE (note.isValid());
    note.setProperty (ids::pitch, (int) note[ids::pitch] + 1, nullptr);

    const auto edited = generated.createCopy();

    SECTION ("kept by default")
    {
        const auto report = ScoreBake::into (project, score, nullptr);

        REQUIRE (report.patternsKept == 1);
        REQUIRE (report.patternsWritten == 0);

        // Untouched, hash included: refreshing the hash here would quietly
        // adopt the edit and let the NEXT compile overwrite it, which makes
        // "your edits are safe" true exactly once.
        REQUIRE (generatedPattern (project).isEquivalentTo (edited));
    }

    SECTION ("replaced when asked, explicitly")
    {
        const auto report = ScoreBake::into (project, score, nullptr,
                                             ScoreBake::Policy::discardHandEdits);

        REQUIRE (report.patternsKept == 0);
        REQUIRE (report.patternsWritten == 1);
        REQUIRE_FALSE (generatedPattern (project).isEquivalentTo (edited));
    }
}

TEST_CASE ("a whole recompile is still one undo step", "[score][bake][recompile]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);

    juce::UndoManager undo;
    ScoreBake::into (project, score, &undo);

    const auto afterFirst = project.createCopy();

    // A second compile removes, rewrites and re-places - the busiest path
    // there is - and still has to collapse into a single press of undo.
    const auto other = compileOrFail (tinyScore ("arrangement {\n  verse x2\n}\n"));
    ScoreBake::into (project, other, &undo);

    REQUIRE_FALSE (project.isEquivalentTo (afterFirst));
    REQUIRE (undo.undo());
    REQUIRE (project.isEquivalentTo (afterFirst));
}

TEST_CASE ("a section the score no longer produces takes its pattern with it",
           "[score][bake][recompile]")
{
    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, 4, nullptr);

    const auto twice = compileOrFail (varyingScore ("  verse x2\n"));
    ScoreBake::into (project, twice, nullptr);
    const auto after = countChildren (project, ids::PATTERN);

    const auto once = compileOrFail (varyingScore ("  verse\n"));
    const auto report = ScoreBake::into (project, once, nullptr);

    REQUIRE (report.patternsRemoved == 1);
    REQUIRE (countChildren (project, ids::PATTERN) == after - 1);
}

TEST_CASE ("an orphaned pattern that was edited by hand is kept", "[score][bake][recompile]")
{
    // Deleting a repeat from the arrangement must not throw away work somebody
    // did on the notes it left behind. It stays in the pattern list, without a
    // clip, where it can be found.
    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, 4, nullptr);

    const auto twice = compileOrFail (varyingScore ("  verse x2\n"));
    ScoreBake::into (project, twice, nullptr);

    auto second = findByGenId (project, "verse#1");
    REQUIRE (second.isValid());

    auto note = second.getChildWithName (ids::NOTE);
    REQUIRE (note.isValid());
    note.setProperty (ids::pitch, (int) note[ids::pitch] + 1, nullptr);

    const auto once = compileOrFail (varyingScore ("  verse\n"));
    const auto report = ScoreBake::into (project, once, nullptr);

    REQUIRE (report.patternsRemoved == 0);
    REQUIRE (report.patternsKept == 1);
    REQUIRE (findByGenId (project, "verse#1").isValid());
}

TEST_CASE ("a renamed generated channel is found again, not duplicated", "[score][bake][recompile]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);
    ScoreBake::into (project, score, nullptr);

    const auto before = countChildren (project, ids::CHANNEL);

    for (auto channel : project)
        if (channel.hasType (ids::CHANNEL) && channel[ids::genId].toString() == "channel:pad")
            channel.setProperty (ids::name, "Warm Pad", nullptr);

    const auto report = ScoreBake::into (project, score, nullptr);

    REQUIRE (report.channelsCreated == 0);
    REQUIRE (countChildren (project, ids::CHANNEL) == before);
}

TEST_CASE ("a pattern hash is about the music, not the bookkeeping", "[score][bake][recompile]")
{
    const auto score = compileOrFail (tinyScore());

    auto project = ProjectFactory::createDefault();
    project.setProperty (ids::stepsPerBeat, score.stepsPerBeat, nullptr);
    ScoreBake::into (project, score, nullptr);

    auto pattern = generatedPattern (project);
    const auto original = ScoreBake::patternHash (pattern);

    SECTION ("renaming a pattern is not an edit to it")
    {
        // Otherwise labelling a pattern would quietly stop the compiler ever
        // updating it again.
        pattern.setProperty (ids::name, "My verse", nullptr);
        REQUIRE (ScoreBake::patternHash (pattern) == original);
    }

    SECTION ("reordering the notes is not an edit either")
    {
        REQUIRE (countChildren (pattern, ids::NOTE) > 1);
        pattern.moveChild (pattern.getNumChildren() - 1, 0, nullptr);
        REQUIRE (ScoreBake::patternHash (pattern) == original);
    }

    SECTION ("moving a note is")
    {
        auto note = pattern.getChildWithName (ids::NOTE);
        note.setProperty (ids::step, (int) note[ids::step] + 1, nullptr);
        REQUIRE (ScoreBake::patternHash (pattern) != original);
    }

    SECTION ("so is changing how loud one is")
    {
        auto note = pattern.getChildWithName (ids::NOTE);
        note.setProperty (ids::velocity, (double) note[ids::velocity] * 0.5, nullptr);
        REQUIRE (ScoreBake::patternHash (pattern) != original);
    }
}
