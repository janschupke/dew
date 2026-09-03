#include <catch2/catch_test_macros.hpp>

#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

int countIn (const juce::ValueTree& tree, const juce::Identifier& type)
{
    int n = 0;

    for (const auto& child : tree)
        if (child.hasType (type))
            ++n;

    return n;
}

} // namespace

TEST_CASE ("the fixture stays small", "[fixture]")
{
    // A guard on a cost, not on a taste. Something like a hundred and thirty
    // assertions render this project, most of them to a buffer and several to a
    // file, so every bar added here is paid for by the whole suite - which is
    // the bill that made the demo and the fixture the same object for so long.
    //
    // A test that wants a longer arrangement should build one, or open a demo.
    const auto project = dew::testing::fixtureProject();

    REQUIRE ((int) project[ids::barsInSong] == 4);
    REQUIRE (countIn (project, ids::PATTERN) == 1);
    REQUIRE (countIn (project, ids::CHANNEL) == 4);
    REQUIRE (countIn (project, ids::AUTOMATION) == 0);
}

TEST_CASE ("the fixture is audible, and is what it says it is", "[fixture]")
{
    // The property the suite actually leans on: four channels that all make a
    // sound, so a render that comes back silent is the change under test and
    // never the fixture.
    const auto project = dew::testing::fixtureProject();
    const auto pattern = project.getChildWithName (ids::PATTERN);

    juce::Array<int> channelsWithNotes;

    for (const auto& note : pattern)
        if (note.hasType (ids::NOTE))
            channelsWithNotes.addIfNotAlreadyThere ((int) note[ids::ch]);

    REQUIRE (channelsWithNotes.size() == 4);

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (project, rendered);

    REQUIRE (report.ok());
    REQUIRE (report.warnings.isEmpty());
    REQUIRE (report.peak > 0.05f);
    REQUIRE (report.peak <= 1.0f);
}

TEST_CASE ("the fixture round-trips through the serializer", "[fixture]")
{
    // It is canonical-shaped, like anything the schema produces. A fixture that
    // was not would make every save-then-load test fail for a reason that had
    // nothing to do with what it was testing.
    const auto project = dew::testing::fixtureProject();
    const auto loaded = ProjectSerializer::fromJsonString (ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());
    REQUIRE (loaded.tree.isEquivalentTo (project));
}
