// The project's grid: how finely a beat is divided.
//
// Its own file rather than a case in MeterTests.cpp, which is what it is
// closest to and is already at the length the tree allows one. The two are
// neighbours and not the same thing: a metre regroups the grid and renames the
// counting, and this cuts it finer.

#include <catch2/catch_test_macros.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/Transport.h"
#include "io/OfflineRenderer.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/NoteTools.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

#include "FixtureProject.h"

using namespace dew;

TEST_CASE ("a project renders identically whatever its grid resolution is", "[grid][render]")
{
    // THE load-bearing test, and the reason setGridResolution rescales anything
    // at all. stepsPerBeat is what Transport::samplesPerStepFor divides by, so
    // doubling it without moving every note halves the duration of the whole
    // project. Verify a change to the rescale by skipping one of its three
    // loops and confirming this fails.
    auto project = dew::testing::fixtureProject();

    RenderOptions options;
    options.mode = Transport::Mode::pattern;
    options.patternId = 1;

    juce::AudioBuffer<float> atFour;
    REQUIRE (OfflineRenderer::renderToBuffer (project, atFour, options).ok());

    ProjectEdits::setGridResolution (project, 8, nullptr);
    REQUIRE (Meter::of (project).stepsPerBeat == 8);

    juce::AudioBuffer<float> atEight;
    REQUIRE (OfflineRenderer::renderToBuffer (project, atEight, options).ok());

    REQUIRE (atFour.getNumSamples() == atEight.getNumSamples());
    REQUIRE (atFour.getNumSamples() > 0);

    int mismatches = 0;

    for (int channel = 0; channel < atFour.getNumChannels(); ++channel)
        for (int i = 0; i < atFour.getNumSamples(); ++i)
            if (! juce::exactlyEqual (atFour.getSample (channel, i),
                                      atEight.getSample (channel, i)))
                ++mismatches;

    INFO ("compared " << (atFour.getNumSamples() * atFour.getNumChannels()) << " samples");
    CHECK (mismatches == 0);
}

TEST_CASE ("raising the grid moves every step-valued thing in the document", "[grid][edits]")
{
    auto project = ProjectFactory::createDefault();
    auto pattern = ProjectEdits::findPattern (project, 1);
    juce::UndoManager undo;

    auto note = ProjectEdits::addNote (pattern, 1, 4, 2, 60, 1.0f, &undo);
    ProjectEdits::fitPatternToNotes (pattern, 16, &undo);

    const auto lengthBefore = (int) pattern[ids::lengthSteps];

    undo.beginNewTransaction ("Change grid resolution");
    ProjectEdits::setGridResolution (project, 8, &undo);

    CHECK ((int) project[ids::stepsPerBeat] == 8);
    CHECK ((int) note[ids::step] == 8);
    CHECK ((int) note[ids::lengthSteps] == 4);
    CHECK ((int) pattern[ids::lengthSteps] == lengthBefore * 2);

    // One transaction, so undo puts the whole document back rather than the
    // setting alone - which would leave every note at twice its step number on
    // a grid that no longer had room for it.
    REQUIRE (undo.undo());

    CHECK ((int) project[ids::stepsPerBeat] == 4);
    CHECK ((int) note[ids::step] == 4);
    CHECK ((int) note[ids::lengthSteps] == 2);
    CHECK ((int) pattern[ids::lengthSteps] == lengthBefore);
}

TEST_CASE ("a clip does not move when the grid changes", "[grid][edits]")
{
    // The asymmetry with setMeter, which HAS to rescale them. A clip is stored
    // in bars, and a bar is a bar whatever it is divided into.
    auto project = ProjectFactory::createDefault();
    juce::UndoManager undo;

    auto track = project.getChildWithName (ids::PLAYLIST).getChild (0);
    auto clip = ProjectEdits::addClip (track, 1, 3, 2, &undo);

    ProjectEdits::setGridResolution (project, 12, &undo);

    CHECK ((int) clip[ids::startBar] == 3);
    CHECK ((int) clip[ids::lengthBars] == 2);
    CHECK ((int) project[ids::barsInSong] >= 5);
}

TEST_CASE ("a division the grid cannot express says so", "[grid][snap]")
{
    // The defect the whole ladder exists to make visible. At four steps to a
    // beat a sixteenth IS a step, so quantizing to it moves every note to
    // itself - which is exactly what "quantize does nothing" was.
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::sixteenth, 4));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::thirtysecond, 4));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::sixteenthTriplet, 4));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::eighthTriplet, 4));

    CHECK (NoteTools::fitsGrid (SnapDivision::eighth, 4));
    CHECK (NoteTools::fitsGrid (SnapDivision::quarter, 4));
    CHECK (NoteTools::fitsGrid (SnapDivision::bar, 4));

    // The absence of a grid fits every grid.
    CHECK (NoteTools::fitsGrid (SnapDivision::off, 4));

    // Eight buys the sixteenth back and the thirty-second becomes the step.
    CHECK (NoteTools::fitsGrid (SnapDivision::sixteenth, 8));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::thirtysecond, 8));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::eighthTriplet, 8));

    // Twelve is what triplets need, and it cannot express a thirty-second.
    CHECK (NoteTools::fitsGrid (SnapDivision::eighthTriplet, 12));
    CHECK (NoteTools::fitsGrid (SnapDivision::sixteenthTriplet, 12));
    CHECK_FALSE (NoteTools::fitsGrid (SnapDivision::thirtysecond, 12));

    // Twenty-four is both.
    for (const auto division : NoteTools::allSnapDivisions)
        CHECK (NoteTools::fitsGrid (division, 24));
}

TEST_CASE ("each grid is named after the finest note it can place", "[grid][snap]")
{
    CHECK (NoteTools::nameForGrid (4) == "1/16");
    CHECK (NoteTools::nameForGrid (8) == "1/32");
    CHECK (NoteTools::nameForGrid (12) == "1/16 T");
    CHECK (NoteTools::nameForGrid (24) == "1/32 T");

    // In 6/8 a beat is an eighth, so every name shifts with it - the same rule
    // nameForSnap follows, and for the same reason.
    CHECK (NoteTools::nameForGrid (4, 8) == "1/32");
}

TEST_CASE ("the snap ladder runs finest to coarsest, with off at the top", "[grid][snap]")
{
    REQUIRE (NoteTools::allSnapDivisions[0] == SnapDivision::off);

    // Every entry is on the ladder exactly once, and indexOfSnap agrees with
    // snapFromIndex - which is what the settings file stores.
    for (int i = 0; i < NoteTools::numSnapDivisions; ++i)
        CHECK (NoteTools::indexOfSnap (NoteTools::snapFromIndex (i)) == i);

    // Coarser as the index grows, at a grid fine enough to tell them apart.
    for (int i = 2; i < NoteTools::numSnapDivisions; ++i)
        CHECK (NoteTools::stepsForSnap (NoteTools::allSnapDivisions[i], 24)
               > NoteTools::stepsForSnap (NoteTools::allSnapDivisions[i - 1], 24));
}
