#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/MixerBus.h"

using namespace dew;
using Catch::Approx;

namespace
{

EngineSnapshot withTracks (std::initializer_list<std::pair<bool, bool>> muteSolo)
{
    EngineSnapshot s;

    for (auto [mute, solo] : muteSolo)
    {
        MixerTrackSnapshot t;
        t.id = (int) s.mixerTracks.size() + 1;
        t.mute = mute;
        t.solo = solo;
        s.anySolo = s.anySolo || solo;
        s.mixerTracks.push_back (t);
    }

    return s;
}

} // namespace

TEST_CASE ("mute silences a track", "[mixer]")
{
    const auto snapshot = withTracks ({ { false, false }, { true, false } });

    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
}

TEST_CASE ("solo anywhere silences every track that is not soloed", "[mixer]")
{
    const auto snapshot = withTracks ({ { false, false }, { false, true }, { false, false } });

    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[2]));
}

TEST_CASE ("mute beats solo on the same track", "[mixer]")
{
    // A track both muted and soloed stays silent: mute is the explicit "off".
    const auto snapshot = withTracks ({ { true, true }, { false, true } });

    REQUIRE (! MixerBus::isAudible (snapshot, snapshot.mixerTracks[0]));
    REQUIRE (MixerBus::isAudible (snapshot, snapshot.mixerTracks[1]));
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
