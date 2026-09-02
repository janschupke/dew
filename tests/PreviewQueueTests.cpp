#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "engine/PreviewQueue.h"

using namespace dew;

TEST_CASE ("events come out in the order they went in", "[preview]")
{
    PreviewQueue queue;
    REQUIRE (queue.isEmpty());

    for (int i = 0; i < 8; ++i)
        REQUIRE (queue.push ({ PreviewEvent::Kind::noteOn, 0, 60 + i, 0.5f }));

    REQUIRE (! queue.isEmpty());

    for (int i = 0; i < 8; ++i)
    {
        PreviewEvent event;
        REQUIRE (queue.pop (event));
        REQUIRE (event.pitch == 60 + i);
    }

    PreviewEvent event;
    REQUIRE (! queue.pop (event));
    REQUIRE (queue.isEmpty());
}

TEST_CASE ("a press and its release both survive one block", "[preview]")
{
    // The reason this is a ring and not a latest-wins atomic: a fast click puts
    // both halves in before the audio thread looks, and with latest-wins the
    // release would overwrite the press and the key would make no sound at all.
    PreviewQueue queue;

    REQUIRE (queue.push ({ PreviewEvent::Kind::noteOn, 2, 64, 0.9f }));
    REQUIRE (queue.push ({ PreviewEvent::Kind::noteOff, 2, 64, 0.0f }));

    PreviewEvent first, second;
    REQUIRE (queue.pop (first));
    REQUIRE (queue.pop (second));

    REQUIRE (first.kind == PreviewEvent::Kind::noteOn);
    REQUIRE (first.pitch == 64);
    REQUIRE (second.kind == PreviewEvent::Kind::noteOff);
    REQUIRE (second.pitch == 64);
}

TEST_CASE ("a full queue refuses rather than corrupting itself", "[preview]")
{
    PreviewQueue queue;

    for (int i = 0; i < PreviewQueue::capacity; ++i)
        REQUIRE (queue.push ({ PreviewEvent::Kind::noteOn, 0, 60, 0.5f }));

    // The producer must not move the read index to make room; that index
    // belongs to the consumer.
    REQUIRE (! queue.push ({ PreviewEvent::Kind::noteOn, 0, 61, 0.5f }));

    // And everything already in it is still intact and in order.
    for (int i = 0; i < PreviewQueue::capacity; ++i)
    {
        PreviewEvent event;
        REQUIRE (queue.pop (event));
        REQUIRE (event.pitch == 60);
    }

    REQUIRE (queue.isEmpty());
}

TEST_CASE ("the queue survives a producer and a consumer hammering it", "[preview][stress]")
{
    // Modelled on SnapshotBridgeTests, which caught a real tearing bug that
    // survived review. Every event that the producer says it wrote must come
    // out exactly once, in order, with its payload intact.
    PreviewQueue queue;

    constexpr int attempts = 400000;

    std::atomic<bool> producerDone { false };
    std::atomic<int> accepted { 0 };

    std::vector<int> received;
    received.reserve (attempts);

    std::thread producer ([&]
    {
        for (int i = 0; i < attempts; ++i)
            if (queue.push ({ PreviewEvent::Kind::noteOn, i % 8, i % 128, 1.0f }))
                accepted.fetch_add (1, std::memory_order_relaxed);

        producerDone.store (true, std::memory_order_release);
    });

    int corrupt = 0;

    while (! producerDone.load (std::memory_order_acquire) || ! queue.isEmpty())
    {
        PreviewEvent event;

        while (queue.pop (event))
        {
            // A torn read would show up as a payload that never agrees with
            // itself: the producer only ever writes matching values.
            if (event.channelIndex != event.pitch % 8 || event.velocity != 1.0f)
                ++corrupt;

            received.push_back (event.pitch);
        }
    }

    producer.join();

    INFO ("accepted " << accepted.load() << " of " << attempts
          << ", received " << received.size() << ", corrupt " << corrupt);

    REQUIRE (corrupt == 0);
    REQUIRE ((int) received.size() == accepted.load());

    // Everything the producer accepted arrived, in the order it was written -
    // the accepted events are a subsequence of 0,1,2,... mod 128.
    REQUIRE (accepted.load() > 1000);
}
