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
    snapshot.barsInSong = (int) (generation % 64) + 1;

    snapshot.channels.resize ((size_t) size);

    for (int i = 0; i < size; ++i)
    {
        snapshot.channels[(size_t) i].id = (int) (generation % 1000) * 1000 + i;
        snapshot.channels[(size_t) i].basePitch = (int) (generation % 128);
    }

    return snapshot;
}

bool isConsistent (const EngineSnapshot& s)
{
    if (s.generation == 0)
        return s.channels.empty();   // the initial, never-published snapshot

    // Exact comparison is the point: this is checking the bytes were not
    // rewritten mid-read, not that a computation came out close.
    if (! juce::exactlyEqual (s.tempoBpm, 100.0 + (double) (s.generation % 100)))
        return false;

    if (s.barsInSong != (int) (s.generation % 64) + 1)
        return false;

    for (size_t i = 0; i < s.channels.size(); ++i)
    {
        if (s.channels[i].id != (int) (s.generation % 1000) * 1000 + (int) i)
            return false;

        if (s.channels[i].basePitch != (int) (s.generation % 128))
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

    std::thread writer ([&]
    {
        for (juce::uint64 generation = 1; ! stop.load(); ++generation)
        {
            // Varying sizes force the vectors to reallocate, which is when a
            // torn read would be most destructive.
            bridge.publish (makeConsistentSnapshot (generation, 1 + (int) (generation % 32)));
        }
    });

    std::thread reader ([&]
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

    std::this_thread::sleep_for (std::chrono::milliseconds (1500));
    stop.store (true);
    writer.join();
    reader.join();

    INFO ("reads: " << readsPerformed.load() << ", highest generation seen: " << highestSeen.load());

    REQUIRE (tornReads.load() == 0);

    // If the reader never saw anything published, the test proved nothing.
    REQUIRE (readsPerformed.load() > 1000);
    REQUIRE (highestSeen.load() > 100);
}
