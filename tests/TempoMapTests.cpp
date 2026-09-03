#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EngineSnapshot.h"
#include "engine/TempoMap.h"
#include "engine/Transport.h"
#include "model/DemoLibrary.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

using namespace dew;
using Catch::Matchers::WithinAbs;

TEST_CASE ("with no tempo automation the map is the arithmetic it replaces", "[tempo]")
{
    // The whole point of isConstant(), and the reason it is a correctness
    // requirement rather than an optimisation: dew's render tests compare
    // samples with juce::exactlyEqual, and rearranging the multiply changes the
    // last bit at some tempos. A project with no tempo curve has to take the
    // arithmetic it has always taken.
    for (const auto bpm : { 20.0, 96.0, 120.0, 124.0, 128.5, 174.0, 333.3, 999.0 })
    {
        for (const auto stepsPerBeat : { 1, 3, 4, 6, 7, 16 })
        {
            for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
            {
                Transport transport;
                transport.prepare (sampleRate);
                transport.setTempo (bpm, stepsPerBeat);

                const auto map = TempoMap::constant (bpm, stepsPerBeat);
                transport.setTempoMap (&map);

                const auto expected = Transport::samplesPerStepFor (bpm, stepsPerBeat, sampleRate);

                for (const auto steps : { 0.0, 1.0, 16.0, 63.0, 1024.0, 99999.0 })
                {
                    INFO (bpm << "bpm, " << stepsPerBeat << " steps/beat, " << sampleRate << "Hz, "
                              << steps << " steps");

                    REQUIRE (juce::exactlyEqual (transport.samplesForSteps (steps),
                                                 expected * steps));
                }
            }
        }
    }
}

TEST_CASE ("a map is exactly invertible at every whole step", "[tempo]")
{
    auto project = ProjectFactory::createDefault();
    juce::StringArray warnings;

    const auto snapshot = buildSnapshot (project, &warnings);
    REQUIRE (snapshot.tempoMap != nullptr);

    const auto& map = *snapshot.tempoMap;

    for (int step = 0; step <= 256; ++step)
    {
        INFO ("step " << step);
        REQUIRE_THAT (map.stepsForSeconds (map.secondsForSteps ((double) step)),
                      WithinAbs ((double) step, 1e-9));
    }

    // And between them, which is where a playhead actually sits.
    for (const auto step : { 0.5, 7.25, 63.125, 200.75 })
    {
        INFO ("step " << step);
        REQUIRE_THAT (map.stepsForSeconds (map.secondsForSteps (step)), WithinAbs (step, 1e-9));
    }
}

TEST_CASE ("a map is strictly monotone", "[tempo]")
{
    // Time going backwards would put the playhead behind itself and make
    // wrappedIntoLoop fold the wrong way.
    auto project = ProjectFactory::createDefault();
    const auto snapshot = buildSnapshot (project, nullptr);

    const auto& map = *snapshot.tempoMap;

    auto previous = map.secondsForSteps (-10.0);

    for (int step = -9; step <= 512; ++step)
    {
        const auto now = map.secondsForSteps ((double) step);

        INFO ("step " << step << ": " << previous << " -> " << now);
        REQUIRE (now > previous);

        previous = now;
    }
}

TEST_CASE ("every committed demo builds a constant map", "[tempo]")
{
    // None of them has a tempo curve, so every one of them must take the fast
    // path - which is what says the guard renders are still comparing what they
    // have always compared.
    const auto& entries = DemoLibrary::entries();
    REQUIRE (! entries.empty());

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        INFO ("demo " << entries[(size_t) i].fileName);

        juce::StringArray warnings;
        const auto tree = DemoLibrary::load (i, warnings);
        REQUIRE (tree.isValid());

        const auto snapshot = buildSnapshot (tree, nullptr);
        REQUIRE (snapshot.tempoMap != nullptr);
        REQUIRE (snapshot.tempoMap->isConstant());
    }
}

TEST_CASE ("a snapshot always has a map, even an empty one", "[tempo]")
{
    // Everything that turns a step into time reads it, so a null check on that
    // path would be a branch in the render loop guarding a state that should not
    // exist.
    const auto empty = buildSnapshot ({}, nullptr);

    REQUIRE (empty.tempoMap != nullptr);
    REQUIRE (empty.tempoMap->isConstant());
}
