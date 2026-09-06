// Where playback begins, and where pause returns to.
//
// dew had no answer to either. Transport::start and stop only ever flipped
// `playing` and never touched the position, AudioEngine offered play, stop and
// rewind, and the only thing that made the transport bar's Stop different from
// the spacebar was that it also called rewind. So there was no way to leave off
// and come back to a chosen place, and no way to say where a piece should start
// from other than by scrubbing to it and never touching Stop.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/Ids.h"
#include "FixtureProject.h"

using namespace dew;

namespace
{

/** A prepared engine on the demo project, stopped at the beginning. */
struct MarkerHarness
{
    MarkerHarness()
    {
        document.setState (dew::testing::fixtureProject(), true);
        engine.prepare (44100.0, 512);
        engine.setProject (document.getState());
        engine.setMode (Transport::Mode::song);
        engine.rewind();
    }

    void render (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            block.clear();
            engine.processBlock (block);
        }
    }

    /** Renders until the playhead is at least `step`, and says whether it got
        there. A block is 512 samples and a step is a few thousand, so counting
        in steps keeps that arithmetic out of every case. */
    bool renderUntilAtLeast (double step, int maxBlocks = 3000)
    {
        for (int i = 0; i < maxBlocks && engine.getPlayheadSteps() < step; ++i)
            render (1);

        return engine.getPlayheadSteps() >= step;
    }

    ProjectDocument document;
    AudioEngine engine;
    juce::AudioBuffer<float> block { 2, 512 };
};

} // namespace

TEST_CASE ("a fresh transport has its marker at the beginning", "[transport][marker]")
{
    // "No marker" needs no state of its own: no marker IS the beginning, which
    // is what makes every case below say one thing rather than two.
    MarkerHarness h;

    CHECK (juce::exactlyEqual (h.engine.getStartMarkerSteps(), 0.0));
    CHECK (h.engine.getPlayheadSteps() < 1.0);
}

TEST_CASE ("setting the marker moves the playhead with it", "[transport][marker]")
{
    // A stopped transport and its marker never disagree, which is what lets the
    // ruler draw one head rather than two things that are usually in the same
    // place. A click on any ruler goes through this call.
    MarkerHarness h;

    h.engine.setStartMarkerSteps (24.0);

    CHECK (juce::exactlyEqual (h.engine.getStartMarkerSteps(), 24.0));
    CHECK (h.engine.getPlayheadSteps() >= 23.5);
    CHECK (h.engine.getPlayheadSteps() <= 24.5);
}

TEST_CASE ("pause returns to the marker and play carries on from there", "[transport][marker]")
{
    MarkerHarness h;

    h.engine.setStartMarkerSteps (16.0);
    h.engine.play();

    REQUIRE (h.renderUntilAtLeast (24.0));

    // Well past the marker, which is the control case: a pause that had done
    // nothing at all would also leave the playhead "at" a marker it never left.
    const auto reached = h.engine.getPlayheadSteps();
    INFO ("reached " << reached << " before pausing");
    REQUIRE (reached > 20.0);

    h.engine.pause();

    CHECK_FALSE (h.engine.isPlaying());
    CHECK (h.engine.getPlayheadSteps() >= 15.5);
    CHECK (h.engine.getPlayheadSteps() <= 16.5);

    // And resuming picks it up from there rather than from where it stopped.
    h.engine.play();
    h.render (1);

    CHECK (h.engine.getPlayheadSteps() >= 15.5);
    CHECK (h.engine.getPlayheadSteps() < reached);
}

TEST_CASE ("stop goes to the beginning, and takes the marker with it", "[transport][marker]")
{
    /*  The other half of the pair, and the reason rewind clears the marker
        rather than only the playhead: the marker is where playback BEGINS, and
        a rewind that left one standing at bar nine would leave the ruler
        showing one place and the next press of space starting at another.
    */
    MarkerHarness h;

    h.engine.setStartMarkerSteps (16.0);
    h.engine.play();

    REQUIRE (h.renderUntilAtLeast (24.0));

    h.engine.stop();
    h.engine.rewind();

    CHECK_FALSE (h.engine.isPlaying());
    CHECK (juce::exactlyEqual (h.engine.getStartMarkerSteps(), 0.0));
    CHECK (h.engine.getPlayheadSteps() < 1.0);

    // So a pause after a stop returns to the beginning, not to the marker that
    // was set before it.
    h.engine.play();
    REQUIRE (h.renderUntilAtLeast (4.0));

    h.engine.pause();

    CHECK (h.engine.getPlayheadSteps() < 1.0);
}

TEST_CASE ("a marker is never before the beginning", "[transport][marker]")
{
    // The same clamp setPlayheadSteps has, and for the same reason: a ruler
    // reports a negative position for a click in its left margin.
    MarkerHarness h;

    h.engine.setStartMarkerSteps (-40.0);

    CHECK (juce::exactlyEqual (h.engine.getStartMarkerSteps(), 0.0));
}
