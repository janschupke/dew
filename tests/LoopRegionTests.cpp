#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/AudioEngine.h"
#include "model/ProjectDocument.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"

using namespace dew;

namespace
{

/** The demo project on a prepared, playing engine.

    The demo is four bars of sixteen steps - 64 - which is what every range below
    is chosen against. Its step is ~5334.7 samples at 124 bpm / 44100, so nothing
    here asserts an exact step: ranges are half-open and tested with >= and <.
*/
struct LoopHarness
{
    explicit LoopHarness (Transport::Mode mode = Transport::Mode::song)
    {
        document.setState (ProjectFactory::createDemo(), true);
        engine.prepare (44100.0, 512);
        engine.setProject (document.getState());
        engine.setMode (mode);
        engine.rewind();
        engine.play();
    }

    /** Renders one block and returns its peak, so a test can say "and it went
        quiet" as well as "and the playhead moved".
    */
    float renderPeak()
    {
        block.clear();
        engine.processBlock (block);
        return block.getMagnitude (0, block.getNumSamples());
    }

    void render (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
            renderPeak();
    }

    /** Renders until the playhead reaches a step, and says whether it got there.

        A block is 512 samples and a step is ~5335, so one block moves the
        playhead about a tenth of a step: reaching step 48 takes five hundred
        blocks, not the fifty a reader would guess. Counting in STEPS rather than
        blocks keeps that arithmetic out of every test.
    */
    bool renderUntilAtLeast (double step, int maxBlocks = 3000)
    {
        for (int i = 0; i < maxBlocks; ++i)
        {
            if (playhead() >= step)
                return true;

            renderPeak();
        }

        return playhead() >= step;
    }

    /** Runs until a block is genuinely loud, so "was sounding" means this block
        rather than the loudest of many. The shape TimelineRulerTests uses.
    */
    float renderUntilLoud()
    {
        float peak = 0.0f;

        for (int i = 0; i < 200 && peak < 0.05f; ++i)
            peak = renderPeak();

        return peak;
    }

    double playhead() { return engine.getPlayheadSteps(); }

    ProjectDocument document;
    AudioEngine engine;
    juce::AudioBuffer<float> block { 2, 512 };
};

constexpr int demoSteps = 64;

} // namespace

TEST_CASE ("a loop region confines the playhead to its range", "[loop][engine]")
{
    LoopHarness h;
    h.engine.setLoopRangeSteps (Transport::Mode::song, 16.0, 32.0);

    // Get inside the region first - a playhead before a loop plays into it
    // rather than being snapped, which is its own test below.
    REQUIRE (h.renderUntilAtLeast (16.0));

    // Well past a whole traversal of the region, so a failure to wrap shows up.
    for (int i = 0; i < 600; ++i)
    {
        h.renderPeak();
        INFO ("playhead " << h.playhead());
        REQUIRE (h.playhead() >= 16.0);
        REQUIRE (h.playhead() < 32.0);
    }
}

TEST_CASE ("with no loop region the playhead still wraps at the end of the material",
           "[loop][engine]")
{
    LoopHarness h;

    // The no-regression case: this is exactly what the engine did before there
    // was any such thing as a loop region.
    for (int i = 0; i < 1200; ++i)
    {
        h.renderPeak();
        REQUIRE (h.playhead() < (double) demoSteps);
    }

    REQUIRE (h.playhead() >= 0.0);
}

TEST_CASE ("a loop region past the end of the material is ignored", "[loop][engine]")
{
    LoopHarness h;
    h.engine.setLoopRangeSteps (Transport::Mode::song, 1000.0, 1016.0);

    // Empty after clamping, so the material's own extent wraps as it always did.
    for (int i = 0; i < 1200; ++i)
    {
        h.renderPeak();
        REQUIRE (h.playhead() < (double) demoSteps);
    }
}

TEST_CASE ("a loop region is clamped to the material", "[loop][engine]")
{
    LoopHarness h;
    h.engine.setLoopRangeSteps (Transport::Mode::song, 48.0, 200.0);

    REQUIRE (h.renderUntilAtLeast (48.0));

    for (int i = 0; i < 600; ++i)
    {
        h.renderPeak();
        INFO ("playhead " << h.playhead());
        REQUIRE (h.playhead() >= 48.0);
        REQUIRE (h.playhead() < (double) demoSteps);
    }
}

TEST_CASE ("clearing a loop region returns the playhead to the material", "[loop][engine]")
{
    LoopHarness h;
    h.engine.setLoopRangeSteps (Transport::Mode::song, 16.0, 32.0);

    REQUIRE (h.renderUntilAtLeast (16.0));
    h.render (100);
    REQUIRE (h.playhead() < 32.0);

    h.engine.clearLoopRange (Transport::Mode::song);

    bool escaped = false;

    for (int i = 0; i < 600 && ! escaped; ++i)
    {
        h.renderPeak();
        escaped = h.playhead() >= 32.0;
    }

    REQUIRE (escaped);
}

TEST_CASE ("a loop dragged backwards means the same as one dragged forwards",
           "[loop][engine]")
{
    LoopHarness h;
    h.engine.setLoopRangeSteps (Transport::Mode::song, 32.0, 16.0);

    const auto region = h.engine.getLoopRegion (Transport::Mode::song);
    REQUIRE (region.startSteps == 16.0f);
    REQUIRE (region.endSteps == 32.0f);

    REQUIRE (h.renderUntilAtLeast (16.0));

    for (int i = 0; i < 400; ++i)
    {
        h.renderPeak();
        REQUIRE (h.playhead() < 32.0);
    }
}

TEST_CASE ("a loop set behind the playhead snaps to its start", "[loop][engine]")
{
    LoopHarness h;

    REQUIRE (h.renderUntilLoud() >= 0.05f);
    REQUIRE (h.renderUntilAtLeast (32.0));

    // Drawn BEHIND the playhead. Folding by modulo would drop it at an arbitrary
    // point inside a region the user has only just drawn, so it goes to the
    // start instead - which is the one thing the relocate branch exists to do.
    h.engine.setLoopRangeSteps (Transport::Mode::song, 0.0, 8.0);
    h.renderPeak();

    INFO ("playhead " << h.playhead());
    REQUIRE (h.playhead() < 8.0);

    // Deliberately NOT asserting that the block went quiet. The voices ARE
    // reset - the relocate resets them for the same reason a seek does - but
    // step 0 is where the demo's kick is, so the very next block is loud again
    // from new notes. The reset is pinned by the seek test in TimelineRulerTests,
    // which jumps somewhere with nothing in it; asserting silence here would be
    // asserting something this code does not promise.
    for (int i = 0; i < 300; ++i)
    {
        h.renderPeak();
        REQUIRE (h.playhead() < 8.0);
    }
}

TEST_CASE ("a loop set ahead of the playhead is played into, not jumped to", "[loop][engine]")
{
    LoopHarness h;
    h.render (4);

    const auto start = h.playhead();
    REQUIRE (start < 8.0);

    h.engine.setLoopRangeSteps (Transport::Mode::song, 32.0, 48.0);

    h.renderPeak();

    // Still where it was, moving forward - not teleported into the region.
    INFO ("playhead " << start << " -> " << h.playhead());
    REQUIRE (h.playhead() > start);
    REQUIRE (h.playhead() < 32.0);
}

TEST_CASE ("the loop region moves when the material shrinks under it", "[loop][engine]")
{
    // Pattern mode, so the material's length is one editable property rather
    // than the extent of every clip in the arrangement. The demo's pattern is
    // sixteen steps - the song is four bars OF it, which is the 64 the song-mode
    // cases above use.
    LoopHarness h { Transport::Mode::pattern };
    h.engine.setLoopRangeSteps (Transport::Mode::pattern, 8.0, 16.0);

    REQUIRE (h.renderUntilAtLeast (8.0));

    for (int i = 0; i < 200; ++i)
    {
        h.renderPeak();
        REQUIRE (h.playhead() >= 8.0);
        REQUIRE (h.playhead() < 16.0);
    }

    // Shorten the pattern underneath the loop. The clamp lives in processBlock
    // and is re-applied every block, so the window has to follow; hoisting the
    // clamp into the setter would leave this looping over steps the pattern no
    // longer has.
    auto pattern = ProjectEdits::findPattern (h.document.getState(), 1);
    REQUIRE (pattern.isValid());
    REQUIRE ((int) pattern[ids::lengthSteps] == 16);

    pattern.setProperty (ids::lengthSteps, 12, nullptr);
    h.engine.setProject (h.document.getState());

    bool cameBack = false;

    for (int i = 0; i < 400 && ! cameBack; ++i)
    {
        h.renderPeak();
        cameBack = h.playhead() < 12.0;
    }

    REQUIRE (cameBack);

    // And it is the CLAMPED window now - [8, 12), not [8, 16) and not the whole
    // pattern.
    for (int i = 0; i < 300; ++i)
    {
        h.renderPeak();
        INFO ("playhead " << h.playhead());
        REQUIRE (h.playhead() >= 8.0);
        REQUIRE (h.playhead() < 12.0);
    }
}

TEST_CASE ("the loop region reads back as the pair that was set", "[loop]")
{
    AudioEngine engine;

    REQUIRE_FALSE (engine.hasLoopRegion (Transport::Mode::pattern));
    REQUIRE_FALSE (engine.hasLoopRegion (Transport::Mode::song));

    engine.setLoopRangeSteps (Transport::Mode::pattern, 8.0, 16.0);

    REQUIRE (engine.hasLoopRegion (Transport::Mode::pattern));
    REQUIRE (engine.getLoopRegion (Transport::Mode::pattern).startSteps == 8.0f);
    REQUIRE (engine.getLoopRegion (Transport::Mode::pattern).endSteps == 16.0f);

    // One region per mode: the piano roll's selection must not become the
    // playlist's, or switching mode would silently change what loops.
    REQUIRE_FALSE (engine.hasLoopRegion (Transport::Mode::song));

    engine.clearLoopRange (Transport::Mode::pattern);
    REQUIRE_FALSE (engine.hasLoopRegion (Transport::Mode::pattern));
}

TEST_CASE ("a negative loop range is clamped rather than inverted", "[loop]")
{
    AudioEngine engine;
    engine.setLoopRangeSteps (Transport::Mode::song, -8.0, 4.0);

    REQUIRE (engine.getLoopRegion (Transport::Mode::song).startSteps == 0.0f);
    REQUIRE (engine.getLoopRegion (Transport::Mode::song).endSteps == 4.0f);
}
