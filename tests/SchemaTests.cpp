#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

/** Structural equality: same type, same properties, same children in order.
    ValueTree::isEquivalentTo does exactly this, but comparing by the serialized
    form as well catches a mismatch the tree comparison would let through.
*/
bool identical (const juce::ValueTree& a, const juce::ValueTree& b)
{
    return a.isEquivalentTo (b);
}

juce::String jsonWithout (const juce::String& json, const juce::String& keyLine)
{
    juce::StringArray lines, kept;
    lines.addLines (json);

    for (const auto& line : lines)
        if (! line.contains (keyLine))
            kept.add (line);

    return kept.joinIntoString ("\n");
}

} // namespace

TEST_CASE ("a project round-trips through JSON unchanged", "[schema]")
{
    const auto original = dew::testing::fixtureProject();

    const auto json = ProjectSerializer::toJsonString (original);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());
    REQUIRE (identical (loaded.tree, original));
}

TEST_CASE ("round-tripping twice is stable", "[schema]")
{
    const auto once  = ProjectSerializer::toJsonString (dew::testing::fixtureProject());
    const auto twice = ProjectSerializer::toJsonString (ProjectSerializer::fromJsonString (once).tree);

    REQUIRE (once == twice);
}

TEST_CASE ("the demo project actually contains notes to play", "[schema][demo]")
{
    const auto demo = dew::testing::fixtureProject();
    const auto pattern = demo.getChildWithName (ids::PATTERN);

    REQUIRE (pattern.isValid());
    REQUIRE (pattern.getNumChildren() > 8);

    // Every channel that has notes must exist, or a render is silent for it.
    for (const auto& note : pattern)
    {
        const int channelId = note[ids::ch];
        bool found = false;

        for (const auto& channel : demo)
            if (channel.hasType (ids::CHANNEL) && (int) channel[ids::id] == channelId)
                found = true;

        INFO ("note references channel " << channelId);
        REQUIRE (found);
    }
}

TEST_CASE ("a property missing from the file falls back to its default", "[schema][compat]")
{
    auto json = ProjectSerializer::toJsonString (ProjectFactory::createDefault());

    // Simulate a file written before "tempoBpm" existed.
    json = jsonWithout (json, "\"tempoBpm\"");

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (juce::exactlyEqual ((double) loaded.tree[ids::tempoBpm], 128.0));

    // Absent is normal for an older file, so it is not worth warning about.
    REQUIRE (loaded.warnings.isEmpty());
}

TEST_CASE ("a property of the wrong type warns and falls back", "[schema][compat]")
{
    auto json = ProjectSerializer::toJsonString (ProjectFactory::createDefault());
    json = json.replace ("\"tempoBpm\": 128.0", "\"tempoBpm\": \"quite fast\"");

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (juce::exactlyEqual ((double) loaded.tree[ids::tempoBpm], 128.0));
    REQUIRE (loaded.warnings.size() == 1);
    REQUIRE (loaded.warnings[0].contains ("tempoBpm"));
}

TEST_CASE ("a key the schema does not know is dropped and reported", "[schema][compat]")
{
    auto json = ProjectSerializer::toJsonString (ProjectFactory::createDefault());
    json = json.replace ("\"tempoBpm\":", "\"swingAmount\": 0.25,\n  \"tempoBpm\":");

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.size() == 1);
    REQUIRE (loaded.warnings[0].contains ("swingAmount"));
    REQUIRE (! loaded.tree.hasProperty (juce::Identifier ("swingAmount")));
}

TEST_CASE ("a file from a newer format version is refused, not half-read", "[schema][compat]")
{
    auto json = ProjectSerializer::toJsonString (ProjectFactory::createDefault());
    json = json.replace ("\"formatVersion\": " + juce::String (kFormatVersion),
                         "\"formatVersion\": 99");

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (! loaded.ok());
    REQUIRE (loaded.result.getErrorMessage().contains ("newer version"));
}

TEST_CASE ("a file written by an older version still loads", "[schema][compat]")
{
    // The compatibility promise, tested against a real v1 payload rather than
    // against a file this build produced: every property added since is filled
    // in from its declared default, and the document comes back current.
    const juce::String v1 = R"({
      "format": "dew-project",
      "formatVersion": 1,
      "name": "Old project",
      "tempoBpm": 96.0,
      "stepsPerBeat": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false,
          "instrument": { "osc": { "wave": "saw", "octave": 0, "detuneCents": 0.0, "gain": 0.8 },
                          "amp": { "attack": 0.005, "decay": 0.12, "sustain": 0.7, "release": 0.15 } } }
      ],
      "patterns": [
        { "id": 1, "name": "Pattern 1", "lengthSteps": 16,
          "notes": [ { "ch": 1, "step": 0, "lengthSteps": 2, "pitch": 40, "velocity": 1.0 } ] }
      ],
      "playlist": { "tracks": [ { "name": "Track 1",
                                  "clips": [ { "patternId": 1, "startBar": 0, "lengthBars": 1 } ] } ] },
      "mixer": { "master": { "gain": 0.9 },
                 "tracks": [ { "id": 1, "name": "Insert 1", "gain": 0.8, "pan": 0.0,
                               "mute": false, "solo": false } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v1);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    REQUIRE (loaded.tree[ids::name].toString() == "Old project");
    REQUIRE (juce::exactlyEqual ((double) loaded.tree[ids::tempoBpm], 96.0));

    // Properties added after v1 take their defaults.
    const auto channel = loaded.tree.getChildWithName (ids::CHANNEL);
    REQUIRE (channel.isValid());
    REQUIRE (channel.hasProperty (ids::solo));
    REQUIRE ((bool) channel[ids::solo] == false);

    const auto track = loaded.tree.getChildWithName (ids::PLAYLIST).getChild (0);
    REQUIRE (track.hasProperty (ids::mute));

    // And the document is now a current-version one.
    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
}

TEST_CASE ("a file that is not a dew project is refused", "[schema][compat]")
{
    SECTION ("valid JSON, wrong format tag")
    {
        const auto loaded = ProjectSerializer::fromJsonString (R"({"format":"ableton","formatVersion":1})");
        REQUIRE (! loaded.ok());
        REQUIRE (loaded.result.getErrorMessage().contains ("not a dew project"));
    }

    SECTION ("not JSON at all")
    {
        const auto loaded = ProjectSerializer::fromJsonString ("RIFF....WAVEfmt");
        REQUIRE (! loaded.ok());
    }

    SECTION ("JSON, but not an object")
    {
        const auto loaded = ProjectSerializer::fromJsonString ("[1, 2, 3]");
        REQUIRE (! loaded.ok());
    }

    SECTION ("empty")
    {
        const auto loaded = ProjectSerializer::fromJsonString ("");
        REQUIRE (! loaded.ok());
    }
}

TEST_CASE ("nested structure survives the round trip", "[schema]")
{
    const auto original = dew::testing::fixtureProject();
    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (original)).tree;

    const auto channel = loaded.getChildWithName (ids::CHANNEL);
    const auto osc = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::OSC);
    REQUIRE (osc.isValid());
    REQUIRE (osc[ids::wave].toString() == "sine");

    const auto clip = loaded.getChildWithName (ids::PLAYLIST)
                            .getChild (0)
                            .getChildWithName (ids::CLIP);
    REQUIRE (clip.isValid());
    REQUIRE ((int) clip[ids::lengthBars] == 4);

    REQUIRE (loaded.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER).isValid());
}

TEST_CASE ("children are ordered by the schema, however the tree was assembled", "[schema]")
{
    const auto canonical = ProjectFactory::createDefault();

    // Rebuild it grouping-by-grouping in the wrong order - playlist and mixer
    // first, then channels and patterns - which is exactly what happens when a
    // tree is assembled by code rather than parsed from a file. Order WITHIN
    // each group is preserved, because channel order is meaningful.
    juce::ValueTree scrambled (ids::PROJECT);

    for (int i = 0; i < canonical.getNumProperties(); ++i)
    {
        const auto key = canonical.getPropertyName (i);
        scrambled.setProperty (key, canonical[key], nullptr);
    }

    // The score comes LAST in the spec, so putting it first is the strongest
    // scramble available - and this list has to name every grouping the schema
    // has, or a new one goes unchecked.
    for (const auto& type : { ids::SCORE, ids::PLAYLIST, ids::MIXER,
                              ids::CHANNEL, ids::PATTERN })
        for (const auto& child : canonical)
            if (child.hasType (type))
                scrambled.appendChild (child.createCopy(), nullptr);

    REQUIRE (scrambled.getNumChildren() == canonical.getNumChildren());
    REQUIRE (! scrambled.isEquivalentTo (canonical));
    REQUIRE (canonicalTree (scrambled, projectSpec()).isEquivalentTo (canonical));
}

TEST_CASE ("a project survives a real save and load through a file", "[schema][io]")
{
    juce::TemporaryFile temp (".dew");
    const auto original = dew::testing::fixtureProject();

    REQUIRE (ProjectSerializer::writeToFile (original, temp.getFile()).wasOk());
    REQUIRE (temp.getFile().existsAsFile());
    REQUIRE (temp.getFile().getSize() > 0);

    const auto loaded = ProjectSerializer::readFromFile (temp.getFile());
    REQUIRE (loaded.ok());
    REQUIRE (identical (loaded.tree, original));
}

TEST_CASE ("the document tracks dirtiness and undo", "[document]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ProjectDocument document;
    REQUIRE (! document.hasChangedSinceSaved());

    auto& undo = document.getUndoManager();

    undo.beginNewTransaction ("rename");
    document.getState().setProperty (ids::name, "Renamed", &undo);

    REQUIRE (document.hasChangedSinceSaved());
    REQUIRE (document.getDocumentTitle() == "Renamed");

    REQUIRE (undo.undo());
    REQUIRE (document.getDocumentTitle() == "Untitled");
}

TEST_CASE ("loading a document replaces its contents and clears undo", "[document]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::TemporaryFile temp (".dew");
    REQUIRE (ProjectSerializer::writeToFile (dew::testing::fixtureProject(), temp.getFile()).wasOk());

    ProjectDocument document;
    auto& undo = document.getUndoManager();
    undo.beginNewTransaction ("edit");
    document.getState().setProperty (ids::name, "Scratch", &undo);

    REQUIRE (document.loadDocument (temp.getFile()).wasOk());

    REQUIRE (document.getDocumentTitle() == "dew fixture");
    REQUIRE (! document.hasChangedSinceSaved());
    REQUIRE (! undo.canUndo());
}
