// Whether a track plays, pan, and the master's own effect chain.
//
// Split out of MixerTests.cpp along its tags; the UI group kept the fixture
// that sat inside it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/MixerBus.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

EngineSnapshot withTracks (std::initializer_list<bool> mutes)
{
    EngineSnapshot s;

    for (auto mute : mutes)
    {
        MixerTrackSnapshot t;
        t.id = (int) s.mixerTracks.size() + 1;
        t.mute = mute;
        s.mixerTracks.push_back (t);
    }

    return s;
}

} // namespace

TEST_CASE ("mute silences a track, and only that track", "[mixer]")
{
    // There were three cases here: mute, "solo anywhere silences every track
    // that is not soloed", and "mute beats solo on the same track". The second
    // and third existed because a track had two flags and they had to be
    // composed - and composing them made one track's audibility a fact about
    // every other track in the mixer, which is what the snapshot's anySolo was
    // precomputed for.
    //
    // One state per track leaves one claim: a track is audible when it is not
    // muted, whatever its neighbours are doing.
    const auto snapshot = withTracks ({ false, true, false });

    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[2]));
}

TEST_CASE ("panning is constant power", "[mixer]")
{
    float left = 0.0f, right = 0.0f;

    MixerBus::panGains (0.0f, left, right);
    REQUIRE (left == Approx (right));
    REQUIRE (left * left + right * right == Approx (1.0f));

    MixerBus::panGains (-1.0f, left, right);
    REQUIRE (left == Approx (1.0f));
    REQUIRE (right == Approx (0.0f).margin (1.0e-6));

    MixerBus::panGains (1.0f, left, right);
    REQUIRE (left == Approx (0.0f).margin (1.0e-6));
    REQUIRE (right == Approx (1.0f));

    // Power stays constant across the sweep - no dip in the middle.
    for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f)
    {
        MixerBus::panGains (pan, left, right);
        REQUIRE (left * left + right * right == Approx (1.0f).epsilon (0.0001));
    }
}

TEST_CASE ("out-of-range pan is clamped rather than wrapped", "[mixer]")
{
    float left = 0.0f, right = 0.0f;
    float clampedLeft = 0.0f, clampedRight = 0.0f;

    MixerBus::panGains (-5.0f, left, right);
    MixerBus::panGains (-1.0f, clampedLeft, clampedRight);

    REQUIRE (left == Approx (clampedLeft));
    REQUIRE (right == Approx (clampedRight));
}

TEST_CASE ("the master carries an effect chain like any other bus", "[mixer][effects]")
{
    // Every insert could hold a chain and the master could not, which read as
    // an omission rather than as a rule.
    auto project = dew::testing::fixtureProject();
    juce::UndoManager undo;

    auto master = project.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());

    auto effect = ProjectEdits::addEffect (project, master, "filter", &undo);
    REQUIRE (effect.isValid());
    effect.setProperty (ids::cutoff, 120.0, &undo);

    juce::StringArray warnings;
    const auto snapshot = buildSnapshot (project, &warnings);

    REQUIRE (warnings.isEmpty());
    REQUIRE (snapshot.masterEffects.numSlots == 1);
    REQUIRE (snapshot.anyEffects);

    RenderOptions options;
    options.seconds = 2.0;
    options.mode = Transport::Mode::pattern;

    juce::AudioBuffer<float> filtered;
    const auto report = OfflineRenderer::renderToBuffer (project, filtered, options);
    REQUIRE (report.ok());

    // A steep lowpass on the master takes the whole mix down, because it is on
    // the summed signal rather than on one track.
    auto plainProject = dew::testing::fixtureProject();
    juce::AudioBuffer<float> plain;
    const auto plainReport = OfflineRenderer::renderToBuffer (plainProject, plain, options);

    INFO ("plain rms " << plainReport.rms << " master-filtered rms " << report.rms);
    REQUIRE (report.rms < plainReport.rms * 0.6f);
    REQUIRE (report.rms > 0.0f);
}

TEST_CASE ("a project without a master chain still loads", "[mixer][schema]")
{
    // formatVersion 4 files have no effects array on the master at all.
    const juce::String version4 = R"({
        "format": "dew-project",
        "formatVersion": 4,
        "name": "older",
        "tempoBpm": 120.0,
        "stepsPerBeat": 4,
        "barsInSong": 4,
        "channels": [],
        "patterns": [],
        "automations": [],
        "playlist": { "tracks": [] },
        "mixer": { "master": { "gain": 0.8 }, "tracks": [] }
    })";

    const auto loaded = ProjectSerializer::fromJsonString (version4);

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.isEmpty());

    const auto master = loaded.tree.getChildWithName (ids::MIXER).getChildWithName (ids::MASTER);
    REQUIRE (master.isValid());
    REQUIRE (juce::exactlyEqual ((double) master[ids::gain], 0.8));
    REQUIRE (ProjectEdits::countEffects (master) == 0);
}
