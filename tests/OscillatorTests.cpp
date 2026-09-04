// Three oscillator slots in the document, across save and load.
//
// Split out of OscillatorTests.cpp along the tags it already carried. The
// shared helpers are OscillatorHarness.h; the engine and UI groups each kept
// the fixture that sat directly above them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "ui/EditorState.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"
#include "OscillatorHarness.h"

using namespace dew;
using namespace dew::testing;
using Catch::Approx;

TEST_CASE ("a channel always carries every oscillator slot, however it was built",
           "[schema][oscillator]")
{
    SECTION ("straight from the schema's defaults")
    {
        requireThreeSlotsWithOnlyTheFirstOn (
            defaultTreeFor (childSpecFor (projectSpec(), "channels")));
    }

    SECTION ("from the factory")
    {
        requireThreeSlotsWithOnlyTheFirstOn (firstChannel (dew::testing::fixtureProject()));
    }

    SECTION ("assembled by hand, with an instrument carrying no oscillator at all")
    {
        // What a test - or a future edit - produces when it appends children
        // itself. canonicalTree is what makes it a full document again.
        juce::ValueTree channel (ids::CHANNEL);
        channel.appendChild (juce::ValueTree (ids::INSTRUMENT), nullptr);

        requireThreeSlotsWithOnlyTheFirstOn (
            canonicalTree (channel, childSpecFor (projectSpec(), "channels")));
    }

    SECTION ("through a save and a load")
    {
        const auto loaded = ProjectSerializer::fromJsonString (
            ProjectSerializer::toJsonString (dew::testing::fixtureProject()));

        REQUIRE (loaded.ok());
        requireThreeSlotsWithOnlyTheFirstOn (firstChannel (loaded.tree));
    }
}

TEST_CASE ("every oscillator keeps its own settings across a round trip", "[schema][oscillator]")
{
    auto project = dew::testing::fixtureProject();
    auto channel = firstChannel (project);

    auto second = ProjectEdits::oscillatorAt (channel, 1);
    second.setProperty (ids::enabled, true, nullptr);
    second.setProperty (ids::wave, "square", nullptr);
    second.setProperty (ids::octave, -1, nullptr);
    second.setProperty (ids::detuneCents, 7.0, nullptr);
    second.setProperty (ids::gain, 0.42, nullptr);

    const auto loaded = ProjectSerializer::fromJsonString (
        ProjectSerializer::toJsonString (project));

    REQUIRE (loaded.ok());
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto reloaded = ProjectEdits::oscillatorAt (firstChannel (loaded.tree), 1);
    REQUIRE ((bool) reloaded[ids::enabled] == true);
    REQUIRE (reloaded[ids::wave].toString() == "square");
    REQUIRE ((int) reloaded[ids::octave] == -1);
    REQUIRE ((double) reloaded[ids::detuneCents] == Approx (7.0));
    REQUIRE ((double) reloaded[ids::gain] == Approx (0.42));

    // The first slot is untouched by any of that.
    const auto first = ProjectEdits::oscillatorAt (firstChannel (loaded.tree), 0);
    REQUIRE (first[ids::wave].toString() != "square");

    // detuneCents is a double in the schema, and has to stay one in the file:
    // an integer there would hand the rounding to the schema's coercion.
    REQUIRE (ProjectSerializer::toJsonString (loaded.tree).contains ("\"detuneCents\": 7.0"));
}

TEST_CASE ("a v5 project's one oscillator becomes the first slot", "[schema][compat][oscillator]")
{
    // A real v5 payload, not one this build wrote: v5 is the last format with a
    // single "osc" object, and its oscillator was by definition playing.
    const juce::String v5 = R"({
      "format": "dew-project",
      "formatVersion": 5,
      "name": "Five",
      "tempoBpm": 120.0,
      "stepsPerBeat": 4,
      "barsInSong": 8,
      "channels": [
        { "id": 1, "name": "Bass", "colour": "ff4fa3ff", "mixerTrackId": 1,
          "basePitch": 40, "volume": 0.7, "pan": 0.0, "muted": false, "solo": false,
          "instrument": { "osc": { "wave": "square", "octave": -1,
                                   "detuneCents": 7.0, "gain": 0.55 },
                          "amp": { "attack": 0.005, "decay": 0.12,
                                   "sustain": 0.7, "release": 0.15 } },
          "effects": [] }
      ],
      "patterns": [ { "id": 1, "name": "Pattern 1", "lengthSteps": 16, "notes": [] } ],
      "automations": [],
      "playlist": { "tracks": [] },
      "mixer": { "master": { "gain": 0.9, "effects": [] },
                 "tracks": [ { "id": 1, "name": "Insert 1", "gain": 0.8, "pan": 0.0,
                               "mute": false, "solo": false, "effects": [] } ] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (v5);

    REQUIRE (loaded.ok());

    // The assertion that proves the migration ran on the parsed JSON rather
    // than on the tree: reaching the schema with a legacy "osc" key would have
    // reported it as one the schema does not know, and dropped it.
    INFO ("warnings: " << loaded.warnings.joinIntoString ("; "));
    REQUIRE (loaded.warnings.isEmpty());

    const auto channel = firstChannel (loaded.tree);
    REQUIRE (ProjectEdits::countOscillators (channel) == kMaxOscillators);

    const auto first = ProjectEdits::oscillatorAt (channel, 0);
    REQUIRE ((bool) first[ids::enabled] == true);
    REQUIRE (first[ids::wave].toString() == "square");
    REQUIRE ((int) first[ids::octave] == -1);
    REQUIRE ((double) first[ids::detuneCents] == Approx (7.0));
    REQUIRE ((double) first[ids::gain] == Approx (0.55));

    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 1)[ids::enabled] == false);
    REQUIRE ((bool) ProjectEdits::oscillatorAt (channel, 2)[ids::enabled] == false);

    REQUIRE ((int) loaded.tree[ids::formatVersion] == kFormatVersion);

    const auto rewritten = ProjectSerializer::toJsonString (loaded.tree);
    REQUIRE (rewritten.contains ("oscillators"));
    REQUIRE (! rewritten.contains ("\"osc\""));
}

TEST_CASE ("a file with more oscillators than the format allows is truncated and reported",
           "[schema][oscillator]")
{
    auto json = ProjectSerializer::toJsonString (dew::testing::fixtureProject());

    // Splice a fourth slot into the first channel's array. No newline in the
    // marker: JSON::toString writes CRLF, and only writeToFile narrows it.
    const auto marker = juce::String ("\"oscillators\": [");
    const auto at = json.indexOf (marker);
    REQUIRE (at >= 0);

    json = json.substring (0, at + marker.length())
           + R"({ "enabled": true, "wave": "sine", "octave": 0, "detuneCents": 0.0, "gain": 0.8 },)"
           + json.substring (at + marker.length());

    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.ok());
    REQUIRE (ProjectEdits::countOscillators (firstChannel (loaded.tree)) == kMaxOscillators);
    REQUIRE (loaded.warnings.joinIntoString ("; ").contains ("oscillators"));
}
