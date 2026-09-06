#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/SignalTap.h"
#include "model/Ids.h"
#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** Writes `count` samples of an ascending ramp on both channels, in blocks, so
    the tap sees the same shape of traffic an audio device produces.
*/
void writeRamp (SignalTap& tap, int count, int blockSize, int startAt = 0)
{
    std::vector<float> block ((size_t) blockSize, 0.0f);

    for (int written = 0; written < count; written += blockSize)
    {
        const auto n = juce::jmin (blockSize, count - written);

        for (int i = 0; i < n; ++i)
            block[(size_t) i] = (float) (startAt + written + i);

        tap.write (block.data(), block.data(), n);
    }
}

float peakOf (const std::vector<float>& samples)
{
    auto peak = 0.0f;

    for (auto sample : samples)
        peak = juce::jmax (peak, std::abs (sample));

    return peak;
}

} // namespace

TEST_CASE ("the tap hands back the newest samples, oldest first", "[signaltap]")
{
    SignalTap tap;
    writeRamp (tap, 3000, 512);

    std::vector<float> window (512, -1.0f);
    REQUIRE (tap.readLatest (window.data(), 512));

    // The last thing written is the last thing in the window, and the order is
    // the order it arrived in - a reversed or off-by-one window fails here.
    for (int i = 0; i < 512; ++i)
        REQUIRE (window[(size_t) i] == Approx ((float) (3000 - 512 + i)));
}

TEST_CASE ("the tap keeps working past the end of the ring", "[signaltap]")
{
    SignalTap tap;

    // Five times round, in blocks, with the ramp continuing across every wrap.
    constexpr int total = SignalTap::capacity * 5;
    writeRamp (tap, total, 512);

    std::vector<float> window ((size_t) SignalTap::maxWindow, -1.0f);
    REQUIRE (tap.readLatest (window.data(), SignalTap::maxWindow));

    for (int i = 0; i < SignalTap::maxWindow; ++i)
        REQUIRE (window[(size_t) i] == Approx ((float) (total - SignalTap::maxWindow + i)));

    REQUIRE (tap.getWriteCount() == total);
}

TEST_CASE ("the tap carries the mean of the pair, not the sum", "[signaltap]")
{
    SignalTap tap;

    std::vector<float> left (256, 1.0f), right (256, 0.0f);
    tap.write (left.data(), right.data(), 256);

    std::vector<float> window (256, -1.0f);
    REQUIRE (tap.readLatest (window.data(), 256));

    for (auto sample : window)
        REQUIRE (sample == Approx (0.5f));

    // And the consequence, stated deliberately: a fully out-of-phase pair reads
    // as silence, because that is what a mono listener hears.
    std::fill (right.begin(), right.end(), -1.0f);
    tap.write (left.data(), right.data(), 256);

    REQUIRE (tap.readLatest (window.data(), 256));

    for (auto sample : window)
        REQUIRE (sample == Approx (0.0f));
}

TEST_CASE ("a tap with less history than asked for pads with silence", "[signaltap]")
{
    SignalTap tap;
    writeRamp (tap, 100, 100, 1);

    std::vector<float> window (512, -1.0f);

    // Succeeds rather than refusing: a display that has just started should show
    // a growing trace, not an empty well.
    REQUIRE (tap.readLatest (window.data(), 512));

    for (int i = 0; i < 412; ++i)
        REQUIRE (window[(size_t) i] == Approx (0.0f));

    for (int i = 0; i < 100; ++i)
        REQUIRE (window[(size_t) (412 + i)] == Approx ((float) (1 + i)));
}

namespace
{

/** Stops the writer and joins it, however the scope is left.

    A stress test's assertion is the one place a raw std::thread cannot be left
    to the two lines after the loop: a failing REQUIRE never reaches them.
*/
struct ScopedWriter
{
    std::atomic<bool>& running;
    std::thread& writer;

    ~ScopedWriter()
    {
        running.store (false);

        if (writer.joinable())
            writer.join();
    }
};

} // namespace

TEST_CASE ("a reader never accepts a window the writer overtook", "[signaltap]")
{
    // The property, not the timing: every window that came back accepted has to
    // be internally consistent. A torn one shows a jump where the writer lapped
    // the copy, and a ramp makes that visible.
    //
    // The ramp cycles through 1..65536 rather than counting up forever, because
    // a float stops representing consecutive integers above 2^24 and this writer
    // gets past that in well under a second - which would make the test fail on
    // its own arithmetic rather than on anything the tap did.
    SignalTap tap;

    constexpr int cycle = 65536;
    const auto rampAt = [] (juce::int64 position) { return (float) (1 + (position % cycle)); };

    std::atomic<bool> running { true };
    std::atomic<int> accepted { 0 }, refused { 0 };

    std::thread writer (
        [&tap, &running, rampAt]
        {
            std::vector<float> block (512, 0.0f);
            juce::int64 position = 0;

            while (running.load())
            {
                for (int i = 0; i < 512; ++i)
                    block[(size_t) i] = rampAt (position + i);

                tap.write (block.data(), block.data(), 512);
                position += 512;
            }
        });

    // Stopped and joined by unwinding, not by the two lines at the end.
    //
    // A REQUIRE below THROWS, so those two lines do not run - and ~thread on a
    // still-joinable thread calls std::terminate. The one test in this suite
    // that could legitimately fail was therefore the one test that could not
    // report it: it aborted the whole ctest process instead, taking every
    // result that had not been written out with it, and the failure read as a
    // crash rather than as the tear it had just found.
    const ScopedWriter stopper { running, writer };

    std::vector<float> window ((size_t) SignalTap::maxWindow, 0.0f);

    // The budget is WORK, not time.
    //
    // This ran for a wall-clock 200ms and then asked only that ONE window had
    // come back accepted - a duration deciding how much of the property gets
    // exercised, and a floor so low it would be met by a single scheduling
    // slice. How much of that 200ms this thread gets is not the test's to know:
    // the suite runs at `ctest --parallel 4`.
    //
    // So the count becomes the TARGET and the clock becomes the escape. The
    // number of windows actually checked stops being a function of the machine,
    // which is what makes a stress test mean the same thing twice.
    constexpr int windowsWanted = 200;

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;

    while (accepted.load() < windowsWanted && juce::Time::getMillisecondCounter() < deadline)
    {
        if (! tap.readLatest (window.data(), SignalTap::maxWindow))
        {
            ++refused;
            continue;
        }

        ++accepted;

        // One assertion per window rather than one per sample: a torn window
        // fails either way, and this does not put a million of them on the clock.
        auto consistent = true;

        for (int i = 1; i < SignalTap::maxWindow && consistent; ++i)
        {
            const auto previous = window[(size_t) (i - 1)];
            const auto current = window[(size_t) i];
            const auto step = current - previous;

            // Exact equality on purpose: these are small whole numbers, written
            // and read back without arithmetic, so anything but the exact value
            // is the tear this is looking for.
            consistent = juce::exactlyEqual (step, 1.0f)                   // the ramp
                         || juce::exactlyEqual (step, (float) (1 - cycle)) // its wrap
                         || (juce::exactlyEqual (previous, 0.0f)           // history it
                             && juce::exactlyEqual (current, 0.0f));       // does not have yet
        }

        REQUIRE (consistent);
    }

    INFO ("accepted " << accepted.load() << ", refused " << refused.load());

    // And it cannot have passed by refusing everything.
    REQUIRE (accepted.load() >= windowsWanted);
}

TEST_CASE ("the tap reports the rate it was prepared at", "[signaltap]")
{
    AudioEngine engine;
    engine.prepare (48000.0, 256);

    REQUIRE (engine.getSignalTap().getSampleRate() == Approx (48000.0));
}

TEST_CASE ("the tap sees the finished master output", "[signaltap][engine]")
{
    // "Non-zero" would be satisfied by a tap on any bus. Sample-for-sample
    // equality with the buffer the host is handed can only be satisfied by a tap
    // on the real one.
    ProjectDocument document;
    document.setState (dew::testing::fixtureProject(), true);

    AudioEngine engine;
    engine.prepare (44100.0, 512);
    engine.setProject (document.getState());
    engine.setMode (Transport::Mode::song);
    engine.play();

    juce::AudioBuffer<float> block (2, 512);
    constexpr int blocks = 86;

    for (int i = 0; i < blocks; ++i)
    {
        block.clear();
        engine.processBlock (block);
    }

    std::vector<float> expected (512, 0.0f);

    for (int i = 0; i < 512; ++i)
        expected[(size_t) i] = 0.5f * (block.getSample (0, i) + block.getSample (1, i));

    std::vector<float> window ((size_t) SignalTap::maxWindow, 0.0f);
    REQUIRE (engine.getSignalTap().readLatest (window.data(), SignalTap::maxWindow));

    for (int i = 0; i < 512; ++i)
        REQUIRE (window[(size_t) (SignalTap::maxWindow - 512 + i)]
                 == Approx (expected[(size_t) i]).margin (1.0e-6));

    // And the project actually made a sound, so none of that passed vacuously.
    REQUIRE (peakOf (window) > 0.01f);
    REQUIRE (engine.getSignalTap().getWriteCount() == (juce::int64) blocks * 512);
}

TEST_CASE ("the master fader moves what the tap sees", "[signaltap][engine]")
{
    // What separates a tap after the master gain from one before it, which the
    // sample-for-sample test above cannot tell apart.
    const auto peakAtGain = [] (float gain)
    {
        auto project = dew::testing::fixtureProject();
        project.getChildWithName (ids::MIXER)
            .getChildWithName (ids::MASTER)
            .setProperty (ids::gain, gain, nullptr);

        AudioEngine engine;
        engine.prepare (44100.0, 512);
        engine.setProject (project);
        engine.setMode (Transport::Mode::song);
        engine.play();

        juce::AudioBuffer<float> block (2, 512);
        auto peak = 0.0f;

        for (int i = 0; i < 86; ++i)
        {
            block.clear();
            engine.processBlock (block);

            std::vector<float> window ((size_t) SignalTap::maxWindow, 0.0f);

            if (engine.getSignalTap().readLatest (window.data(), SignalTap::maxWindow))
                peak = juce::jmax (peak, peakOf (window));
        }

        return peak;
    };

    const auto loud = peakAtGain (1.0f);
    const auto quiet = peakAtGain (0.25f);

    REQUIRE (loud > 0.01f);
    REQUIRE (quiet == Approx (loud * 0.25f).epsilon (0.1));
}

TEST_CASE ("silence reaches the tap as silence, and the count still moves", "[signaltap][engine]")
{
    // The pair the display depends on: a device delivering silence must not look
    // like no device at all.
    ProjectDocument document;
    document.setState (ProjectFactory::createDefault(), true);

    AudioEngine engine;
    engine.prepare (44100.0, 512);
    engine.setProject (document.getState());
    engine.play();

    juce::AudioBuffer<float> block (2, 512);

    for (int i = 0; i < 20; ++i)
    {
        block.clear();
        engine.processBlock (block);
    }

    std::vector<float> window ((size_t) SignalTap::maxWindow, 0.0f);
    REQUIRE (engine.getSignalTap().readLatest (window.data(), SignalTap::maxWindow));

    REQUIRE (peakOf (window) == Approx (0.0f));
    REQUIRE (engine.getSignalTap().getWriteCount() == 20 * 512);
}
