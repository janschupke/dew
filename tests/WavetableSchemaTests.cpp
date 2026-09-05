// Wavetable oscillators in the DOCUMENT: what a round trip keeps, and what an
// older file becomes.
//
// Split from WavetableEngineTests.cpp when that file reached the length gate,
// and the seam is the honest one: everything here asks what is in the tree and
// nothing here makes a sound.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "engine/Wavetable.h"
#include "model/AutomationTargets.h"
#include "model/GeneratorCatalog.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

TEST_CASE ("a wavetable slot keeps every setting across a round trip", "[schema][wavetable]")
{
    auto project = dew::testing::fixtureProject();
    auto channel = project.getChildWithName (ids::CHANNEL);

    auto slot = ProjectEdits::oscillatorAt (channel, 1);
    slot.setProperty (ids::enabled, true, nullptr);
    slot.setProperty (ids::mode, "wavetable", nullptr);
    auto wavetable = generatorNodeFor (slot, ids::wavePosition);
    wavetable.setProperty (ids::wavetable, "formant", nullptr);
    wavetable.setProperty (ids::wavePosition, 0.375, nullptr);
    wavetable.setProperty (ids::wavePositionMod, -0.5, nullptr);
    wavetable.setProperty (ids::wavePositionSource, "lfo", nullptr);
    wavetable.setProperty (ids::wavePositionRate, 3.25, nullptr);
    wavetable.setProperty (ids::unisonVoices, 5, nullptr);
    wavetable.setProperty (ids::unisonDetune, 12.5, nullptr);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::oscillatorAt (loaded.tree.getChildWithName (ids::CHANNEL),
                                                      1);

    REQUIRE (reloaded[ids::mode].toString() == "wavetable");
    const auto reloadedTable = generatorNodeFor (reloaded, ids::wavePosition);
    REQUIRE (reloadedTable[ids::wavetable].toString() == "formant");
    REQUIRE ((double) reloadedTable[ids::wavePosition] == Approx (0.375));
    REQUIRE ((double) reloadedTable[ids::wavePositionMod] == Approx (-0.5));
    REQUIRE (reloadedTable[ids::wavePositionSource].toString() == "lfo");
    REQUIRE ((double) reloadedTable[ids::wavePositionRate] == Approx (3.25));
    REQUIRE ((int) reloadedTable[ids::unisonVoices] == 5);
    REQUIRE ((double) reloadedTable[ids::unisonDetune] == Approx (12.5));

    // The declared default's TYPE is what the reader coerces a file value to,
    // so the types have to survive the trip: a voice count written as 5.0 would
    // hand the rounding to the schema, and a position written as 0 would stop
    // being a double the moment someone set it to a whole number.
    REQUIRE (json.contains ("\"unisonVoices\": 5"));
    REQUIRE (json.contains ("\"wavePosition\": 0.375"));
    REQUIRE (json.contains ("\"wavePositionRate\": 3.25"));
}

TEST_CASE ("a v7 project loads as classic oscillators", "[schema][compat][wavetable]")
{
    // v7 is the last format without a mode. Every oscillator in one was by
    // definition the classic generator, and nothing in the file says so - which
    // is exactly what a declared default is for. No migration should run.
    const juce::String v7 = R"({
      "format": "dew-project",
      "formatVersion": 7,
      "name": "Seven",
      "tempoBpm": 120.0,
      "stepsPerBeat": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false,
          "source": "synth",
          "instrument": { "oscillators": [
                            { "enabled": true, "wave": "square", "octave": -1,
                              "detuneCents": 7.0, "gain": 0.55 } ],
                          "amp": { "attack": 0.005, "decay": 0.12,
                                   "sustain": 0.7, "release": 0.15 } },
          "effects": [] }
      ],
      "patterns": [ { "id": 1, "name": "Pattern 1", "lengthSteps": 16, "notes": [] } ],
      "automations": [],
      "playlist": { "tracks": [] },
      "mixer": { "master": { "gain": 0.9, "effects": [] },
                 "tracks": [ { "id": 1, "name": "Insert 1", "gain": 0.8, "pan": 0.0,
                               "mute": false, "effects": [] } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v7);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto channel = loaded.tree.getChildWithName (ids::CHANNEL);
    const auto first = ProjectEdits::oscillatorAt (channel, 0);

    REQUIRE (first[ids::mode].toString() == "classic");
    REQUIRE (generatorNodeFor (first, ids::wave)[ids::wave].toString() == "square");

    const auto firstTable = generatorNodeFor (first, ids::wavePosition);
    REQUIRE (firstTable[ids::wavetable].toString() == "basic");
    REQUIRE ((int) firstTable[ids::unisonVoices] == 1);

    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
}

TEST_CASE ("a table name this build does not have is reported, not silently swapped",
           "[schema][wavetable]")
{
    auto project = dew::testing::fixtureProject();
    auto slot = ProjectEdits::oscillatorAt (project.getChildWithName (ids::CHANNEL), 0);

    slot.setProperty (ids::mode, "wavetable", nullptr);
    generatorNodeFor (slot, ids::wavetable)
        .setProperty (ids::wavetable, "something-from-the-future", nullptr);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (warnings.joinIntoString ("; ").contains ("something-from-the-future"));
    REQUIRE (snapshot.channels.front().osc.slots[0].table == 0);

    // A CLASSIC slot never reads the table, so an unknown name there is not
    // worth a warning about a sound nobody is making.
    slot.setProperty (ids::mode, "classic", nullptr);

    warnings.clear();
    buildSnapshot (project, &warnings);

    REQUIRE (warnings.isEmpty());
}

TEST_CASE ("a v14 project's flat oscillator settings move onto their generators",
           "[schema][compat][wavetable]")
{
    // The migration v15 needs. A v14 slot holds all thirteen properties flat;
    // a v15 one holds the slot's five and a node per generator. Both halves are
    // moved, not only the one the slot is running - a classic slot still
    // carries whatever wavetable settings somebody dialled in before switching
    // back, and dropping them here would make loading and saving a way to lose
    // them.
    const auto v14 = R"({
      "format": "dew-project",
      "formatVersion": 14,
      "channels": [ { "id": 1, "name": "Pad", "source": "synth",
        "instrument": { "oscillators": [ {
          "enabled": true, "octave": 1, "detuneCents": 4.0, "gain": 0.6,
          "mode": "wavetable", "wave": "square",
          "wavetable": "formant", "wavePosition": 0.25, "wavePositionMod": -0.75,
          "wavePositionSource": "lfo", "wavePositionRate": 2.5,
          "unisonVoices": 3, "unisonDetune": 9.0 } ] } } ]
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v14);

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString (" | "));
    CHECK (loaded.warnings.isEmpty());

    const auto channel = loaded.tree.getChildWithName (ids::CHANNEL);
    const auto slot = ProjectEdits::oscillatorAt (channel, 0);
    REQUIRE (slot.isValid());

    // The SLOT's own stay where they were.
    CHECK ((bool) slot[ids::enabled]);
    CHECK ((int) slot[ids::octave] == 1);
    CHECK ((double) slot[ids::detuneCents] == Approx (4.0));
    CHECK ((double) slot[ids::gain] == Approx (0.6));
    CHECK (slot[ids::mode].toString() == "wavetable");

    // And each generator's went to its own node - including the classic half,
    // which this slot is not running.
    const auto classic = generatorNodeFor (slot, ids::wave);
    REQUIRE (classic.isValid());
    CHECK (classic[ids::wave].toString() == "square");

    const auto table = generatorNodeFor (slot, ids::wavePosition);
    REQUIRE (table.isValid());
    CHECK (table[ids::wavetable].toString() == "formant");
    CHECK ((double) table[ids::wavePosition] == Approx (0.25));
    CHECK ((double) table[ids::wavePositionMod] == Approx (-0.75));
    CHECK (table[ids::wavePositionSource].toString() == "lfo");
    CHECK ((double) table[ids::wavePositionRate] == Approx (2.5));
    CHECK ((int) table[ids::unisonVoices] == 3);
    CHECK ((double) table[ids::unisonDetune] == Approx (9.0));

    // Nothing is left flat on the slot: the whole point of the move.
    for (const auto* property :
         { &ids::wave, &ids::wavetable, &ids::wavePosition, &ids::unisonDetune })
    {
        INFO ("still flat: " << property->toString());
        CHECK_FALSE (slot.hasProperty (*property));
    }

    // And it is stamped as the version it now IS.
    CHECK ((int) loaded.tree[ids::formatVersion] == kFormatVersion);
}

TEST_CASE ("a slot writes only the generator it has something to say about", "[schema][wavetable]")
{
    // The point of the move. Every classic slot in every project used to store
    // seven wavetable properties nothing would ever read - twelve of them per
    // example file - and nesting them alone would only have grouped them.
    //
    // Both nodes are still there IN MEMORY, which is what keeps the canonical
    // tree one shape and lets the panel point at a generator before you have
    // committed to it. A file does not need the editor's convenience.
    auto project = ProjectFactory::createDefault();
    auto channel = project.getChildWithName (ids::CHANNEL);
    auto slot = ProjectEdits::oscillatorAt (channel, 0);

    REQUIRE (slot.getChildWithName (ids::CLASSIC).isValid());
    REQUIRE (slot.getChildWithName (ids::WAVETABLE).isValid());

    const auto fresh = ProjectSerializer::toJsonString (project);

    INFO (fresh.substring (0, 600));
    CHECK_FALSE (fresh.contains ("wavePositionRate"));
    CHECK (fresh.contains ("\"wave\""));

    // LOSSLESS, and that is why the rule is "when default" rather than "when
    // inert": a slot somebody dialled a wavetable into and then switched back
    // still writes it, so changing generator is not a way to lose the other
    // one's settings on the next save.
    auto wavetable = generatorNodeFor (slot, ids::wavePosition);
    wavetable.setProperty (ids::wavePositionRate, 7.5, nullptr);

    const auto dialled = ProjectSerializer::toJsonString (project);
    CHECK (dialled.contains ("wavePositionRate"));

    const auto loaded = ProjectSerializer::fromJsonString (dialled);
    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString (" | "));
    CHECK (loaded.warnings.isEmpty());

    const auto reloaded = generatorNodeFor (
        ProjectEdits::oscillatorAt (loaded.tree.getChildWithName (ids::CHANNEL), 0),
        ids::wavePosition);

    CHECK ((double) reloaded[ids::wavePositionRate] == Approx (7.5));

    // And a file that omitted the node reads it back at its defaults, so a
    // round trip over the omitted case is the same document either way.
    const auto plain = ProjectSerializer::fromJsonString (fresh);
    REQUIRE (plain.ok());
    CHECK (plain.warnings.isEmpty());

    const auto materialised = generatorNodeFor (
        ProjectEdits::oscillatorAt (plain.tree.getChildWithName (ids::CHANNEL), 0),
        ids::wavePosition);

    REQUIRE (materialised.isValid());
    CHECK ((double) materialised[ids::wavePositionRate]
           == Approx ((double) requireInstrumentParamSpec (ids::wavePositionRate).defaultValue));
}
