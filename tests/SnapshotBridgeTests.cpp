#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "engine/SnapshotBridge.h"

using namespace dew;

namespace
{

/** A snapshot whose contents are derivable from its generation, so a reader can
    tell whether what it is holding is internally consistent. A torn read - the
    writer overwriting a slot mid-read - shows up as fields that do not agree.
*/
EngineSnapshot makeConsistentSnapshot (juce::uint64 generation, int size)
{
    EngineSnapshot snapshot;
    snapshot.generation = generation;
    snapshot.tempoBpm = 100.0 + (double) (generation % 100);
    snapshot.masterGain = 0.5f + (float) (generation % 32) / 64.0f;

    snapshot.channels.resize ((size_t) size);

    for (int i = 0; i < size; ++i)
    {
        snapshot.channels[(size_t) i].id = (int) (generation % 1000) * 1000 + i;
        snapshot.channels[(size_t) i].volume = 0.25f + (float) (generation % 16) / 32.0f;
    }

    return snapshot;
}

bool isConsistent (const EngineSnapshot& s)
{
    if (s.generation == 0)
        return s.channels.empty(); // the initial, never-published snapshot

    // Exact comparison is the point: this is checking the bytes were not
    // rewritten mid-read, not that a computation came out close.
    //
    // The markers are deliberately of different widths at different offsets - a
    // double and a float at the top, an int and a float per channel - so a tear
    // that happened to leave one of them intact still shows up in another.
    if (! juce::exactlyEqual (s.tempoBpm, 100.0 + (double) (s.generation % 100)))
        return false;

    if (! juce::exactlyEqual (s.masterGain, 0.5f + (float) (s.generation % 32) / 64.0f))
        return false;

    for (size_t i = 0; i < s.channels.size(); ++i)
    {
        if (s.channels[i].id != (int) (s.generation % 1000) * 1000 + (int) i)
            return false;

        if (! juce::exactlyEqual (s.channels[i].volume,
                                  0.25f + (float) (s.generation % 16) / 32.0f))
            return false;
    }

    return true;
}

} // namespace

TEST_CASE ("a fresh bridge reads a valid empty snapshot", "[bridge]")
{
    SnapshotBridge bridge;

    const auto& snapshot = bridge.acquire();
    REQUIRE (snapshot.channels.empty());
    REQUIRE (snapshot.generation == 0);
}

TEST_CASE ("a published snapshot becomes visible to the reader", "[bridge]")
{
    SnapshotBridge bridge;

    bridge.publish (makeConsistentSnapshot (7, 3));

    const auto& snapshot = bridge.acquire();
    REQUIRE (snapshot.generation == 7);
    REQUIRE (snapshot.channels.size() == 3);
    REQUIRE (isConsistent (snapshot));
}

TEST_CASE ("the reader keeps its snapshot until it acquires again", "[bridge]")
{
    SnapshotBridge bridge;

    bridge.publish (makeConsistentSnapshot (1, 1));
    const auto& held = bridge.acquire();
    REQUIRE (held.generation == 1);

    // Two publishes while the reader is "in a block".
    bridge.publish (makeConsistentSnapshot (2, 1));
    bridge.publish (makeConsistentSnapshot (3, 1));

    // The reference it is holding must not have been rewritten underneath it.
    REQUIRE (held.generation == 1);
    REQUIRE (isConsistent (held));

    REQUIRE (bridge.acquire().generation == 3);
}

TEST_CASE ("a reader never sees a torn snapshot under a continuous writer", "[bridge][stress]")
{
    // The safety argument for the three-slot rotation is not obvious by
    // inspection, so it gets exercised rather than trusted: a writer publishing
    // flat out against a reader latching flat out, checking every snapshot it
    // sees is self-consistent.
    SnapshotBridge bridge;

    std::atomic<bool> stop { false };
    std::atomic<int> tornReads { 0 };
    std::atomic<juce::uint64> readsPerformed { 0 };
    std::atomic<juce::uint64> highestSeen { 0 };

    std::thread writer (
        [&]
        {
            for (juce::uint64 generation = 1; ! stop.load(); ++generation)
            {
                // Varying sizes force the vectors to reallocate, which is when a
                // torn read would be most destructive.
                bridge.publish (makeConsistentSnapshot (generation, 1 + (int) (generation % 32)));
            }
        });

    std::thread reader (
        [&]
        {
            while (! stop.load())
            {
                const auto& snapshot = bridge.acquire();

                // Read it the way the audio thread would: touch every field, more
                // than once, over a window long enough for a writer to interfere.
                for (int pass = 0; pass < 4; ++pass)
                    if (! isConsistent (snapshot))
                        tornReads.fetch_add (1);

                auto previous = highestSeen.load();
                while (snapshot.generation > previous
                       && ! highestSeen.compare_exchange_weak (previous, snapshot.generation))
                {
                }

                readsPerformed.fetch_add (1);
            }
        });

    // The budget is WORK, not time.
    //
    // This slept a wall-clock 1500ms and then required 1000 reads and 100
    // generations - a duration and a rate, set independently, with nothing
    // holding them in step. The suite runs at `ctest --parallel 4`, so how much
    // of that 1500ms this thread actually gets is not the test's to know. It
    // has not been caught failing, and the margin today is enormous; it is the
    // SHAPE that is wrong, because the day it does fail it will fail on the
    // machine rather than on the property, and the property - that no acquired
    // snapshot is ever torn - is the only thing here worth failing on.
    //
    // So the two numbers become the TARGET and the clock becomes the escape.
    // That also stops the suite sleeping a second and a half to prove something
    // it has proved within a few milliseconds.
    constexpr juce::uint64 readsWanted = 1000;
    constexpr juce::uint64 generationsWanted = 100;

    const auto deadline = juce::Time::getMillisecondCounter() + 30000;

    while (juce::Time::getMillisecondCounter() < deadline
           && (readsPerformed.load() < readsWanted || highestSeen.load() < generationsWanted))
        juce::Thread::sleep (1);

    stop.store (true);
    writer.join();
    reader.join();

    INFO ("reads: " << readsPerformed.load()
                    << ", highest generation seen: " << highestSeen.load());

    REQUIRE (tornReads.load() == 0);

    // If the reader never saw anything published, the test proved nothing.
    REQUIRE (readsPerformed.load() >= readsWanted);
    REQUIRE (highestSeen.load() >= generationsWanted);
}
