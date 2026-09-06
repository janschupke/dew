#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"

using namespace dew;

namespace
{

int countChildren (const juce::ValueTree& tree, const juce::Identifier& type)
{
    auto count = 0;

    for (const auto& child : tree)
        if (child.hasType (type))
            ++count;

    return count;
}

/** Writes `text` into a fresh project, saves it, loads it, and gives back what
    came out. The round trip is the point: anything that survives only in memory
    is not stored.
*/
juce::String throughAFile (const juce::String& text)
{
    auto project = ProjectFactory::createDefault();
    ProjectEdits::setScoreSource (project, text, "t.score", nullptr);

    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    return ProjectEdits::scoreSource (loaded.tree);
}

} // namespace

TEST_CASE ("the score is stored in the project, one node per line", "[score][schema]")
{
    auto project = ProjectFactory::createDefault();

    ProjectEdits::setScoreSource (project, "song {\n  tempo 96\n}\n", "amber.score", nullptr);

    const auto score = project.getChildWithName (ids::SCORE);
    REQUIRE (score.isValid());
    REQUIRE (score[ids::name].toString() == "amber.score");

    // A node per line, and not one string holding the lot: juce::JSON writes a
    // newline as \n, so a whole score in one property is a single enormous line
    // that changes entirely whenever a comma moves - in files that tests and
    // people both read as diffs.
    REQUIRE (countChildren (score, ids::LINE) == 4);
    REQUIRE (score.getChild (1)[ids::text].toString() == "  tempo 96");
}

TEST_CASE ("score text survives a save and a load exactly", "[score][schema]")
{
    SECTION ("the ordinary case")
    {
        const juce::String source = "song {\n  title \"Amber\"\n}\n";
        REQUIRE (throughAFile (source) == source);
    }

    SECTION ("no trailing newline")
    {
        // The difference between a file that ends in a newline and one that
        // does not is real, and losing it would make merely opening a project
        // rewrite its score.
        REQUIRE (throughAFile ("song { }") == "song { }");
    }

    SECTION ("blank lines in the middle and at the end")
    {
        REQUIRE (throughAFile ("a\n\n\nb\n\n") == "a\n\n\nb\n\n");
    }

    SECTION ("nothing at all")
    {
        auto project = ProjectFactory::createDefault();
        ProjectEdits::setScoreSource (project, "", "", nullptr);

        // No text is no lines, not one empty one - so a project nobody has
        // written a score for stays byte-identical to one whose score was
        // cleared.
        REQUIRE (countChildren (project.getChildWithName (ids::SCORE), ids::LINE) == 0);
        REQUIRE (ProjectEdits::scoreSource (project).isEmpty());
    }

    SECTION ("non-ASCII text")
    {
        // Comments carry em dashes and names carry accents, and a byte-oriented
        // split would cut one in half.
        const juce::String source = juce::CharPointer_UTF8 ("// a note \xe2\x80\x94 in F\n"
                                                            "song { }\n");
        REQUIRE (throughAFile (source) == source);
    }

    SECTION ("CRLF is normalised, once and for all")
    {
        // Stored as LF so a diagnostic's byte offsets mean the same thing
        // wherever the file came from.
        REQUIRE (throughAFile ("a\r\nb\r\n") == "a\nb\n");
    }
}

TEST_CASE ("writing a score twice replaces it rather than appending", "[score][schema]")
{
    auto project = ProjectFactory::createDefault();

    ProjectEdits::setScoreSource (project, "one\ntwo\nthree\n", "a.score", nullptr);
    ProjectEdits::setScoreSource (project, "x\n", "b.score", nullptr);

    REQUIRE (ProjectEdits::scoreSource (project) == "x\n");
    REQUIRE (ProjectEdits::scoreSourceName (project) == "b.score");
}

TEST_CASE ("the score is one undo step", "[score][schema]")
{
    auto project = ProjectFactory::createDefault();
    const auto before = project.createCopy();

    juce::UndoManager undo;
    undo.beginNewTransaction ("Edit score");
    ProjectEdits::setScoreSource (project, "song {\n  tempo 96\n}\n", "t.score", &undo);

    REQUIRE (ProjectEdits::scoreSource (project).isNotEmpty());
    REQUIRE (undo.undo());
    REQUIRE (project.isEquivalentTo (before));
}

TEST_CASE ("a project from before the score loads as one nobody compiled",
           "[score][schema][compat]")
{
    // The additive-defaults claim, asserted against a real v9 payload rather
    // than argued: an empty score and empty provenance, no migration, and not
    // one warning.
    const juce::String v9 = R"({
      "format": "dew-project",
      "formatVersion": 9,
      "name": "Older project",
      "tempoBpm": 96.0,
      "stepsPerBeat": 4,
      "beatsPerBar": 4,
      "beatUnit": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false, "solo": false,
          "source": "synth" }
      ],
      "patterns": [
        { "id": 1, "name": "Pattern 1", "lengthSteps": 16,
          "notes": [ { "ch": 1, "step": 0, "lengthSteps": 2, "pitch": 40, "velocity": 1.0 } ] }
      ],
      "playlist": { "tracks": [ { "name": "Track 1", "mute": false, "solo": false,
                                  "clips": [ { "kind": "pattern", "patternId": 1,
                                               "startBar": 0, "lengthBars": 1 } ] } ] },
      "mixer": { "master": { "gain": 0.9 },
                 "tracks": [ { "id": 1, "name": "Insert 1", "gain": 0.8, "pan": 0.0,
                               "mute": false, "solo": false } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v9);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto score = loaded.tree.getChildWithName (ids::SCORE);
    REQUIRE (score.isValid());
    REQUIRE (countChildren (score, ids::LINE) == 0);

    // Nothing in it was generated, and every provenance string says so.
    const auto pattern = loaded.tree.getChildWithName (ids::PATTERN);
    REQUIRE (pattern.hasProperty (ids::genId));
    REQUIRE (pattern[ids::genId].toString().isEmpty());
    REQUIRE (pattern[ids::genHash].toString().isEmpty());

    REQUIRE (loaded.tree.getChildWithName (ids::CHANNEL)[ids::genId].toString().isEmpty());

    const auto track = loaded.tree.getChildWithName (ids::PLAYLIST).getChild (0);
    REQUIRE (track[ids::genId].toString().isEmpty());
    REQUIRE (track.getChild (0)[ids::genId].toString().isEmpty());

    // And it is now a current-version document.
    //
    // The literal is a TRIPWIRE, not a fact about the score: it fires on every
    // bump so that whoever bumps has to come back here and confirm this v9
    // payload still loads without a warning. It has done that once already -
    // v11 made an automation point's step a double, v12 gave a playlist track
    // and a mixer strip a colour, v13 gave every channel a SOUNDFONT node, and
    // v14 added four effect types and the nine parameters they brought, v15
    // moved a slot's generator parameters onto that generator's node, and v16
    // gave a playlist track a gain - so update the number when the rest of this
    // test still passes, and do not delete it.
    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
    REQUIRE (kFormatVersion == 18);
}
