#include <catch2/catch_approx.hpp>

#include <cmath>
#include <catch2/catch_test_macros.hpp>

#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSchema.h"
#include "model/ProjectSerializer.h"
#include "engine/EngineSnapshot.h"
#include "engine/Sequencer.h"
#include "engine/Transport.h"
#include "ui/design/Tokens.h"
#include "ui/primitives/DewMeter.h"
#include "io/OfflineRenderer.h"
#include "engine/AudioEngine.h"
#include "ui/EditorState.h"
#include "ui/TimelineRuler.h"
#include "ui/TimelineView.h"
#include "ui/TransportBar.h"
#include "app/ProjectDocument.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

juce::ValueTree clipAt (juce::ValueTree project, int trackIndex)
{
    auto track = project.getChildWithName (ids::PLAYLIST).getChild (trackIndex);

    for (auto clip : track)
        if (clip.hasType (ids::CLIP))
            return clip;

    return {};
}

} // namespace

TEST_CASE ("a bar is as many beats as the meter says", "[meter]")
{
    auto project = ProjectFactory::createDefault();

    CHECK (Meter::of (project).stepsPerBar() == 16);

    project.setProperty (ids::beatsPerBar, 3, nullptr);
    CHECK (Meter::of (project).stepsPerBar() == 12);

    project.setProperty (ids::beatsPerBar, 7, nullptr);
    project.setProperty (ids::stepsPerBeat, 3, nullptr);
    CHECK (Meter::of (project).stepsPerBar() == 21);
}

TEST_CASE ("an absent meter is 4/4, not a clamp", "[meter]")
{
    // A tree built by hand - a test, or the factory mid-construction - has no
    // meter properties at all. Clamping a missing 0 would make it 1/1 and put a
    // bar line between every step.
    juce::ValueTree bare (ids::PROJECT);

    const auto meter = Meter::of (bare);

    CHECK (meter.beatsPerBar == 4);
    CHECK (meter.beatUnit == 4);
    CHECK (meter.stepsPerBeat == 4);
}

TEST_CASE ("a meter written outside dew is clamped rather than trusted", "[meter]")
{
    auto project = ProjectFactory::createDefault();

    project.setProperty (ids::beatsPerBar, 0, nullptr);
    CHECK (Meter::of (project).beatsPerBar == 1);

    project.setProperty (ids::beatsPerBar, 999, nullptr);
    CHECK (Meter::of (project).beatsPerBar == Meter::maxBeatsPerBar);

    // A denominator is a note value, so a power of two. Ties round up, matching
    // what juce::MidiMessage does with the same number.
    project.setProperty (ids::beatUnit, 6, nullptr);
    CHECK (Meter::of (project).beatUnit == 8);

    project.setProperty (ids::beatUnit, 3, nullptr);
    CHECK (Meter::of (project).beatUnit == 4);

    project.setProperty (ids::beatUnit, 5, nullptr);
    CHECK (Meter::of (project).beatUnit == 4);

    project.setProperty (ids::beatUnit, 32, nullptr);
    CHECK (Meter::of (project).beatUnit == 16);
}

TEST_CASE ("the meter round-trips, and a file without one loads as 4/4", "[meter][schema]")
{
    auto project = ProjectFactory::createDefault();
    ProjectEdits::setMeter (project, 7, 8, nullptr);

    const auto json = ProjectSerializer::toJsonString (project);
    const auto loaded = ProjectSerializer::fromJsonString (json);

    REQUIRE (loaded.tree.isValid());
    CHECK (loaded.warnings.isEmpty());
    CHECK (Meter::of (loaded.tree).beatsPerBar == 7);
    CHECK (Meter::of (loaded.tree).beatUnit == 8);

    // An older file simply predates the properties.
    auto older = ProjectSerializer::toJsonString (ProjectFactory::createDefault());
    older = older.replace ("\"formatVersion\": " + juce::String (kFormatVersion),
                           "\"formatVersion\": 8");

    auto parsed = juce::JSON::parse (older);
    parsed.getDynamicObject()->removeProperty (ids::beatsPerBar);
    parsed.getDynamicObject()->removeProperty (ids::beatUnit);

    const auto legacy = ProjectSerializer::fromJsonString (juce::JSON::toString (parsed));

    REQUIRE (legacy.tree.isValid());
    CHECK (legacy.warnings.isEmpty());
    CHECK (Meter::of (legacy.tree).beatsPerBar == 4);
    CHECK (Meter::of (legacy.tree).beatUnit == 4);
}

TEST_CASE ("changing the meter does not change how long a step is", "[meter][timing]")
{
    // The whole point of the type. A meter regroups the grid; it is not a
    // tempo, so the thing every duration in the engine is built out of has to
    // come out identical.
    auto project = ProjectFactory::createDefault();

    const auto before = Transport::samplesPerStepFor ((double) project[ids::tempoBpm],
                                                      Meter::of (project).stepsPerBeat, 48000.0);

    ProjectEdits::setMeter (project, 3, 4, nullptr);

    const auto after = Transport::samplesPerStepFor ((double) project[ids::tempoBpm],
                                                     Meter::of (project).stepsPerBeat, 48000.0);

    CHECK (juce::exactlyEqual (before, after));
}

TEST_CASE ("changing the meter holds the arrangement's position in steps", "[meter][edits]")
{
    // A clip is stored in BARS, so redefining a bar would move every clip
    // boundary and silence whatever fell outside the new window. setMeter
    // rescales instead, and 16 -> 8 divides evenly so nothing has to round.
    auto project = dew::testing::fixtureProject();

    auto clip = clipAt (project, 0);
    REQUIRE (clip.isValid());

    clip.setProperty (ids::startBar, 4, nullptr);
    clip.setProperty (ids::lengthBars, 2, nullptr);

    const auto oldStepsPerBar = Meter::of (project).stepsPerBar();
    const auto startSteps = 4 * oldStepsPerBar;
    const auto lengthSteps = 2 * oldStepsPerBar;

    auto exact = false;
    ProjectEdits::setMeter (project, 2, 4, nullptr, &exact);

    const auto newStepsPerBar = Meter::of (project).stepsPerBar();
    REQUIRE (newStepsPerBar == 8);

    CHECK (exact);
    CHECK ((int) clip[ids::startBar] * newStepsPerBar == startSteps);
    CHECK ((int) clip[ids::lengthBars] * newStepsPerBar == lengthSteps);
}

TEST_CASE ("a meter that cannot divide evenly rounds, and says so", "[meter][edits]")
{
    auto project = dew::testing::fixtureProject();

    auto clip = clipAt (project, 0);
    REQUIRE (clip.isValid());

    clip.setProperty (ids::startBar, 1, nullptr);
    clip.setProperty (ids::lengthBars, 1, nullptr);

    // 16 steps to 12 is a ratio of 4/3, so a clip at bar 1 wants bar 1.33.
    auto exact = true;
    ProjectEdits::setMeter (project, 3, 4, nullptr, &exact);

    CHECK_FALSE (exact);
    CHECK ((int) clip[ids::lengthBars] >= 1);
}

TEST_CASE ("the notational denominator moves no bar line", "[meter][edits]")
{
    auto project = dew::testing::fixtureProject();

    auto clip = clipAt (project, 0);
    REQUIRE (clip.isValid());

    clip.setProperty (ids::startBar, 3, nullptr);
    clip.setProperty (ids::lengthBars, 2, nullptr);

    auto exact = false;
    ProjectEdits::setMeter (project, 4, 8, nullptr, &exact);

    CHECK (exact);
    CHECK ((int) clip[ids::startBar] == 3);
    CHECK ((int) clip[ids::lengthBars] == 2);
    CHECK (Meter::of (project).beatUnit == 8);
}

TEST_CASE ("setting the meter it already has changes nothing", "[meter][edits]")
{
    auto project = dew::testing::fixtureProject();
    const auto before = project.createCopy();

    ProjectEdits::setMeter (project, 4, 4, nullptr);

    CHECK (project.isEquivalentTo (before));
}

TEST_CASE ("the snapshot carries the meter to the engine", "[meter][engine]")
{
    auto project = dew::testing::fixtureProject();
    ProjectEdits::setMeter (project, 3, 8, nullptr);

    const auto snapshot = buildSnapshot (project, nullptr);

    CHECK (snapshot.beatsPerBar == 3);
    CHECK (snapshot.beatUnit == 8);
    CHECK (snapshot.stepsPerBar() == snapshot.stepsPerBeat * 3);
}

TEST_CASE ("the ruler recovers the beat from the meter, not from a four", "[meter][ruler]")
{
    // TimelineRuler used to derive stepsPerBeat as stepsPerBar / 4, which put
    // beat ticks on non-beats in every meter but 4/4 - in all three editors at
    // once, since they share this one routine. stepForClick is the pure-math
    // half of the same Style, so it pins the field being carried at all.
    TimelineView timeline;
    timeline.pixelsPerStep = 10.0;
    timeline.scrollOffsetSteps = 0.0;

    const juce::Rectangle<int> bounds { 0, 0, 480, 24 };

    ruler::Style style;
    style.stepsPerBar = 12; // 3/4 at four steps to the beat
    style.beatsPerBar = 3;
    style.totalSteps = 96;

    // Bar three starts at step 24, and lands where the timeline says it does.
    CHECK (ruler::stepForClick ((int) timeline.xForStep (24.0), bounds, timeline, style.totalSteps)
           == Catch::Approx (24.0).margin (0.5));

    // The beat the ruler would tick is stepsPerBar / beatsPerBar - four steps,
    // the project's stepsPerBeat - rather than stepsPerBar / 4, which is three.
    CHECK (style.stepsPerBar / style.beatsPerBar == 4);
}

TEST_CASE ("the position readout counts to the numerator", "[meter][transport]")
{
    Meter threeFour;
    threeFour.stepsPerBeat = 4;
    threeFour.beatsPerBar = 3;

    const Meter fourFour;

    // Step 12 is the step that tells the two apart: the start of the second bar
    // in 3/4, still the last beat of the first in 4/4.
    CHECK (TransportBar::positionText (12.0, threeFour) == "002:1:1");
    CHECK (TransportBar::positionText (12.0, fourFour) == "001:4:1");

    CHECK (TransportBar::positionText (0.0, threeFour) == "001:1:1");
    CHECK (TransportBar::positionText (8.0, threeFour) == "001:3:1");
    CHECK (TransportBar::positionText (11.0, threeFour) == "001:3:4");

    // 7/8: seven beats to the bar, so the eighth beat starts the next one.
    Meter sevenEight;
    sevenEight.stepsPerBeat = 2;
    sevenEight.beatsPerBar = 7;
    sevenEight.beatUnit = 8;

    CHECK (TransportBar::positionText (13.0, sevenEight) == "001:7:2");
    CHECK (TransportBar::positionText (14.0, sevenEight) == "002:1:1");

    // A negative position is a clamp, not a bar zero.
    CHECK (TransportBar::positionText (-4.0, fourFour) == "001:1:1");
}

TEST_CASE ("a pattern renders identically whatever the meter says", "[meter][render]")
{
    // The load-bearing test. A meter is not a tempo: it regroups the grid and
    // renames the counting, and a pattern - whose material is measured in steps
    // rather than bars - must come out of the engine sample for sample the
    // same. Anything that folded the meter into a duration would fail here.
    auto project = dew::testing::fixtureProject();

    RenderOptions options;
    options.mode = Transport::Mode::pattern;
    options.patternId = 1;

    juce::AudioBuffer<float> inFourFour;
    REQUIRE (OfflineRenderer::renderToBuffer (project, inFourFour, options).ok());

    ProjectEdits::setMeter (project, 3, 4, nullptr);
    REQUIRE (Meter::of (project).stepsPerBar() == 12);

    juce::AudioBuffer<float> inThreeFour;
    REQUIRE (OfflineRenderer::renderToBuffer (project, inThreeFour, options).ok());

    REQUIRE (inFourFour.getNumSamples() == inThreeFour.getNumSamples());
    REQUIRE (inFourFour.getNumSamples() > 0);

    int mismatches = 0;

    for (int channel = 0; channel < inFourFour.getNumChannels(); ++channel)
        for (int i = 0; i < inFourFour.getNumSamples(); ++i)
            if (! juce::exactlyEqual (inFourFour.getSample (channel, i),
                                      inThreeFour.getSample (channel, i)))
                ++mismatches;

    INFO ("compared " << (inFourFour.getNumSamples() * inFourFour.getNumChannels()) << " samples");
    CHECK (mismatches == 0);
}

TEST_CASE ("rescaling holds a song render's length across a meter change", "[meter][render]")
{
    // The other half of the same promise, in song mode. Clips are stored in
    // bars, so without the rescale in setMeter a 3/4 song would be a quarter
    // shorter and every clip would start earlier. With it, the arrangement
    // keeps its length in steps - and 16 to 8 divides evenly, so exactly.
    auto project = dew::testing::fixtureProject();

    const auto stepsBefore = buildSnapshot (project, nullptr).songLengthSteps();
    const auto stepsPerBarBefore = Meter::of (project).stepsPerBar();

    auto exact = false;
    ProjectEdits::setMeter (project, 2, 4, nullptr, &exact);
    REQUIRE (exact);

    const auto stepsAfter = buildSnapshot (project, nullptr).songLengthSteps();

    CHECK (Meter::of (project).stepsPerBar() == stepsPerBarBefore / 2);
    CHECK (stepsAfter == stepsBefore);
}

TEST_CASE ("a meter falls at the same speed whatever the frame rate", "[ui][meter]")
{
    // The defect the time constant replaces. Three widgets multiplied by 0.82
    // or 0.8 per tick, which means a meter falls at half speed if its timer is
    // halved and at double speed on a dropped frame - and the scope's own
    // comment said so rather than fixing it.
    //
    // Sampled across the WHOLE fall rather than at its ends: a decay that
    // matched only at 300ms would still be the wrong shape in between.
    constexpr int totalMs = 300;

    auto coarse = 1.0f;
    coarse = dew::meter::fall (coarse, 0.0f, totalMs);

    auto fine = 1.0f;

    for (int i = 0; i < 10; ++i)
        fine = dew::meter::fall (fine, 0.0f, totalMs / 10);

    INFO ("one " << totalMs << "ms step gave " << coarse << ", ten gave " << fine);
    CHECK (std::abs (coarse - fine) < 1.0e-4f);

    // And it is a fall, not a jump or a hold.
    CHECK (coarse < 1.0f);
    CHECK (coarse > 0.0f);

    // Half the time constant is more than half the fall: an exponential, not a
    // straight line down.
    const auto halfway = dew::meter::fall (1.0f, 0.0f, dew::tokens::motion::meterReleaseMs / 2);
    CHECK (halfway > 0.5f);
}

TEST_CASE ("a meter rises the instant a peak arrives", "[ui][meter]")
{
    // Load-bearing twice: a meter that rose as slowly as it falls would miss
    // every transient, and every test of a level here asserts after ONE frame
    // rather than waiting for a ramp.
    CHECK (juce::exactlyEqual (dew::meter::fall (0.0f, 0.9f, 33), 0.9f));
    CHECK (juce::exactlyEqual (dew::meter::fall (0.5f, 0.5f, 33), 0.5f));

    // And it reaches silence rather than approaching it forever, so an idle
    // meter is a state a test can wait for.
    auto level = 1.0f;

    for (int i = 0; i < 200; ++i)
        level = dew::meter::fall (level, 0.0f, 33);

    CHECK (juce::exactlyEqual (level, 0.0f));
}

TEST_CASE ("a meter is scaled the way a level is heard", "[ui][meter]")
{
    // Linear - which the settings panel's input meter was - puts a healthy mix
    // in the bottom fifth of the bar and reads as broken.
    CHECK (juce::exactlyEqual (dew::meter::proportionForGain (0.0f), 0.0f));
    CHECK (dew::meter::proportionForGain (1.0f) > 0.99f);

    // Half amplitude is about six dB down, which on a 48dB scale is seven
    // eighths of the way up - not half.
    const auto half = dew::meter::proportionForGain (0.5f);
    INFO ("half amplitude reads " << half);
    CHECK (half > 0.8f);
    CHECK (half < 0.95f);

    // Below the floor is empty, and the scale rises all the way.
    CHECK (juce::exactlyEqual (dew::meter::proportionForGain (0.001f), 0.0f));
    CHECK (dew::meter::proportionForGain (0.5f) > dew::meter::proportionForGain (0.25f));
}
