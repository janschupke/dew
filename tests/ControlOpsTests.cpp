#include <catch2/catch_test_macros.hpp>

#include "ControlHarness.h"
#include "control/ParamAddress.h"
#include "model/Ids.h"
#include "model/ProjectFactory.h"
#include "model/ProjectEdits.h"

using namespace dew;
using namespace dew::control;
using namespace dew::testing;

namespace
{

int noteCount (const juce::ValueTree& pattern)
{
    auto count = 0;

    for (const auto& child : pattern)
        if (child.hasType (ids::NOTE))
            ++count;

    return count;
}

} // namespace

TEST_CASE ("project_describe names everything without reading a note", "[control]")
{
    FakeHost host;

    const auto result = call (host, "project_describe");
    REQUIRE (result.ok);

    const auto value = result.value;

    REQUIRE ((double) value[juce::Identifier ("tempoBpm")] > 0.0);
    REQUIRE (value[juce::Identifier ("channels")].getArray()->size() > 0);
    REQUIRE (value[juce::Identifier ("patterns")].getArray()->size() > 0);

    // The claim that makes it the cheap read: a pattern is reported by its NOTE
    // COUNT and never by its notes. Without this the summary grows with the
    // song and the tool it exists to be cheaper than.
    const auto pattern = value[juce::Identifier ("patterns")].getArray()->getReference (0);
    REQUIRE ((int) pattern[juce::Identifier ("notes")] > 0);
    REQUIRE (pattern[juce::Identifier ("notes")].isArray() == false);
}

TEST_CASE ("a batch of notes is one undo step", "[control][undo]")
{
    FakeHost host;
    const auto pattern = ProjectEdits::findPattern (host.project(), 1);
    const auto before = noteCount (pattern);

    juce::Array<juce::var> notes;

    for (auto i = 0; i < 32; ++i)
        notes.add (Fields {}
                       .with ("channelId", 1)
                       .with ("step", 64 + i)
                       .with ("pitch", 60)
                       .with ("lengthSteps", 1)
                       .with ("velocity", 0.7));

    const auto result = call (host, "notes_write",
                              Fields {}.with ("patternId", 1).with ("notes", juce::var (notes)));

    REQUIRE (result.ok);
    REQUIRE ((int) result.value[juce::Identifier ("added")] == 32);
    REQUIRE (noteCount (pattern) == before + 32);

    // The promise the consent dialog rests on. Thirty-two notes, one Cmd-Z.
    REQUIRE (host.undoDepth() == 1);

    host.undo.undo();
    REQUIRE (noteCount (pattern) == before);
}

TEST_CASE ("writing the same note twice does not double it", "[control]")
{
    FakeHost host;
    const auto pattern = ProjectEdits::findPattern (host.project(), 1);

    const auto once = Fields {}
                          .with ("patternId", 1)
                          .with ("notes", list ({ Fields {}
                                                      .with ("channelId", 1)
                                                      .with ("step", 100)
                                                      .with ("pitch", 64)
                                                      .with ("lengthSteps", 4)
                                                      .with ("velocity", 0.5) }));

    REQUIRE (call (host, "notes_write", once).ok);
    const auto after = noteCount (pattern);

    // An upsert on the three fields that IDENTIFY a note, so a caller re-sending
    // a bar it has already sent gets the bar it meant rather than two of it.
    const auto again = call (host, "notes_write", once);

    REQUIRE (again.ok);
    REQUIRE ((int) again.value[juce::Identifier ("added")] == 0);
    REQUIRE ((int) again.value[juce::Identifier ("changed")] == 1);
    REQUIRE (noteCount (pattern) == after);
}

TEST_CASE ("a batch with one bad entry writes none of it", "[control]")
{
    FakeHost host;
    const auto pattern = ProjectEdits::findPattern (host.project(), 1);
    const auto before = noteCount (pattern);

    const auto good = Fields {}
                          .with ("channelId", 1)
                          .with ("step", 200)
                          .with ("pitch", 60)
                          .with ("lengthSteps", 1)
                          .with ("velocity", 0.8);

    // Pitch 200 is outside what the editor can show. The entry AFTER the good
    // one, deliberately: a check that ran as it wrote would already have
    // written the first.
    const auto bad = Fields {}
                         .with ("channelId", 1)
                         .with ("step", 201)
                         .with ("pitch", 200)
                         .with ("lengthSteps", 1)
                         .with ("velocity", 0.8);

    const auto result = call (host, "notes_write",
                              Fields {}.with ("patternId", 1).with ("notes", list ({ good, bad })));

    REQUIRE_FALSE (result.ok);
    REQUIRE (result.error.contains ("pitch"));

    // Half a batch is the worst answer available. Nothing moved.
    REQUIRE (noteCount (pattern) == before);
    REQUIRE (host.undoDepth() == 0);
}

TEST_CASE ("params_write reaches a parameter automation cannot", "[control]")
{
    FakeHost host;

    const auto channel = ProjectEdits::findChannel (host.project(), 1);
    const auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);
    REQUIRE (amp.isValid());

    const auto result = call (host, "params_write",
                              Fields {}.with ("entries", list ({ Fields {}
                                                                     .with ("target", "channel")
                                                                     .with ("id", 1)
                                                                     .with ("group", "amp")
                                                                     .with ("param", "release")
                                                                     .with ("value", 1.25) })));

    REQUIRE (result.ok);
    REQUIRE ((int) result.value[juce::Identifier ("applied")] == 1);
    REQUIRE ((double) amp[ids::release] > 1.0);
    REQUIRE (host.flushes > 0);
}

TEST_CASE ("a value outside a parameter's range is clamped, not refused", "[control]")
{
    FakeHost host;
    const auto channel = ProjectEdits::findChannel (host.project(), 1);

    const auto result = call (host, "params_write",
                              Fields {}.with ("entries", list ({ Fields {}
                                                                     .with ("target", "channel")
                                                                     .with ("id", 1)
                                                                     .with ("param", "volume")
                                                                     .with ("value", 1000.0) })));

    REQUIRE (result.ok);

    const auto spec = paramSpecFor (host.project(),
                                    []
                                    {
                                        ParamAddress address;
                                        address.target = "channel";
                                        address.id = 1;
                                        address.param = ids::volume;
                                        return address;
                                    }());

    REQUIRE (spec.has_value());
    REQUIRE ((double) channel[ids::volume] <= spec->maximum);
}

TEST_CASE ("a parameter that does not exist is refused by name", "[control]")
{
    FakeHost host;

    const auto result = call (host, "params_write",
                              Fields {}.with ("entries", list ({ Fields {}
                                                                     .with ("target", "channel")
                                                                     .with ("id", 1)
                                                                     .with ("param", "loudness")
                                                                     .with ("value", 1.0) })));

    REQUIRE_FALSE (result.ok);
    REQUIRE (result.error.contains ("loudness"));
    REQUIRE (result.error.contains ("params_list"));
}

TEST_CASE ("an unknown argument is refused rather than ignored", "[control]")
{
    // Silently dropping a misspelled argument is how a note comes out at the
    // default velocity with nothing anywhere saying why.
    const auto* op = findOp ("structure_write");
    REQUIRE (op != nullptr);

    const auto fault = validateArgs (op->args, Fields {}.with ("tempoBmp", 120.0));

    REQUIRE (fault.contains ("tempoBmp"));
    REQUIRE (fault.contains ("is not an argument"));
}

TEST_CASE ("clips and lanes are placed as one undo step and grow the song", "[control][undo]")
{
    FakeHost host;

    REQUIRE (call (host, "playlist_tracks_write",
                   Fields {}.with ("entries", list ({ Fields {}.with ("name", "Agent lane") })))
                 .ok);

    const auto result = call (host, "clips_write",
                              Fields {}.with ("clips", list ({ Fields {}
                                                                   .with ("track", 0)
                                                                   .with ("startBar", 24)
                                                                   .with ("lengthBars", 4)
                                                                   .with ("patternId", 1) })));

    REQUIRE (result.ok);
    REQUIRE ((int) result.value[juce::Identifier ("applied")] == 1);

    // Grows to fit, and says so. Trailing empty bars are a deliberate silence,
    // so nothing shrinks behind the caller's back.
    REQUIRE ((bool) result.value[juce::Identifier ("songGrew")]);
    REQUIRE ((int) result.value[juce::Identifier ("barsInSong")] >= 28);
}

TEST_CASE ("the metre rescales the arrangement and reports whether it was exact", "[control][undo]")
{
    FakeHost host;

    const auto result = call (host, "structure_write",
                              Fields {}.with ("beatsPerBar", 3).with ("beatUnit", 4));

    REQUIRE (result.ok);
    REQUIRE (host.project()[ids::beatsPerBar] == juce::var (3));

    // A clip is stored in BARS, so redefining a bar moves every clip - and the
    // rescale is exact only when the ratio divides. The caller is told which.
    REQUIRE (result.value.hasProperty (juce::Identifier ("clipsLandedOnWholeBars")));
}

TEST_CASE ("automation_write answers with the automation's id, not the clip's", "[control]")
{
    FakeHost host;

    const auto result = call (host, "automation_write",
                              Fields {}
                                  .with ("target", "channel")
                                  .with ("id", 1)
                                  .with ("param", "volume")
                                  .with ("startBar", 0)
                                  .with ("lengthBars", 4));

    REQUIRE (result.ok);

    // The model's addAutomationWithClip returns the CLIP - it was written for a
    // caller that wanted somewhere to scroll to - and a clip carries no id of
    // its own, so reading one off it gave every caller 0. The id matters
    // because it is what automation_points_write takes.
    const auto id = (int) result.value[juce::Identifier ("id")];

    REQUIRE (id > 0);
    REQUIRE (ProjectEdits::findAutomation (host.project(), id).isValid());
    REQUIRE (result.value[juce::Identifier ("name")].toString().isNotEmpty());

    // And that id really does address the curve.
    const auto shaped = call (
        host, "automation_points_write",
        Fields {}
            .with ("id", id)
            .with ("replace", true)
            .with ("points", list ({ Fields {}.with ("step", 0.0).with ("value", 0.2),
                                     Fields {}.with ("step", 32.0).with ("value", 0.9) })));

    REQUIRE (shaped.ok);
    REQUIRE ((int) shaped.value[juce::Identifier ("points")] == 2);
}

TEST_CASE ("an operation needing the transport says so rather than crashing", "[control]")
{
    // FakeHost keeps ControlHost's refusing defaults, which is exactly what a
    // build with no audio device does.
    FakeHost host;

    const auto result = call (host, "transport_write", Fields {}.with ("playing", true));

    REQUIRE_FALSE (result.ok);
    REQUIRE (result.error.contains ("transport"));
}

TEST_CASE ("score_compile turns text into notes as one undo step", "[control][undo]")
{
    // The committed example rather than a score written here. A score invented
    // in a test proves the compiler rejects what its author guessed at; this
    // one is the language as it actually is, and CI already renders it.
    const juce::File source { juce::String (DEW_EXAMPLES_DIR) + "/amber.score" };
    REQUIRE (source.existsAsFile());

    // An EMPTY project, because a project that already has notes refuses to
    // take a score's grid and metre - and the fixture has eighteen.
    FakeHost host { ProjectFactory::createDefault() };

    const auto stored = call (host, "score_write",
                              Fields {}.with ("source", source.loadFileAsString()));
    REQUIRE (stored.ok);

    INFO (juce::JSON::toString (stored.value[juce::Identifier ("diagnostics")]));
    REQUIRE ((bool) stored.value[juce::Identifier ("compiles")]);

    // Storing writes NO notes, deliberately: text being worked on should not
    // fill the undo stack with them.
    REQUIRE (host.undoDepth() == 1);

    const auto read = call (host, "score_read");
    REQUIRE (read.ok);
    REQUIRE ((bool) read.value[juce::Identifier ("compiles")]);

    const auto baked = call (host, "score_compile");

    INFO (juce::JSON::toString (baked.value));
    REQUIRE (baked.ok);
    REQUIRE ((int) baked.value[juce::Identifier ("notesWritten")] > 0);
    REQUIRE ((int) baked.value[juce::Identifier ("clipsWritten")] > 0);

    // The whole bake is one step, however many patterns and notes it produced.
    REQUIRE (host.undoDepth() == 2);
}

TEST_CASE ("a score with errors bakes nothing", "[control]")
{
    FakeHost host { ProjectFactory::createDefault() };

    const auto result = call (host, "score_compile",
                              Fields {}.with ("source", "song\n  tempo not a tempo\n"));

    REQUIRE_FALSE (result.ok);
    REQUIRE (result.error.contains ("error"));
    REQUIRE (host.undoDepth() == 0);
}
