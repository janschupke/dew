// The effects themselves: what each one does to a signal.
//
// Split out of an EffectTests.cpp that was 1,746 lines, along the Catch2
// tags it already carried.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/AudioEngine.h"
#include "engine/EngineSnapshot.h"
#include "io/OfflineRenderer.h"
#include "model/ProjectEdits.h"
#include "model/ProjectFactory.h"
#include "model/ProjectSerializer.h"

#include "EffectDspHarness.h"
#include "FixtureProject.h"

using namespace dew;
using namespace dew::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE ("a lowpass removes high content and keeps low content", "[effects][dsp]")
{
    Params params { EffectType::filter };
    params.setMode (FilterMode::lowpass);
    params.set (ids::cutoff, 200.0f);
    params.set (ids::resonance, 0.5f);

    // The same filter, on two tones an order of magnitude apart in frequency.
    auto low = sineBuffer (80.0, 22050);
    auto high = sineBuffer (5000.0, 22050);

    const auto lowBefore = rmsOf (low, 11025, 11025);
    const auto highBefore = rmsOf (high, 11025, 11025);

    runEffect (params, low);
    runEffect (params, high);

    const auto lowAfter = rmsOf (low, 11025, 11025);
    const auto highAfter = rmsOf (high, 11025, 11025);

    INFO ("80Hz " << lowBefore << " -> " << lowAfter << ", 5kHz " << highBefore << " -> "
                  << highAfter);

    REQUIRE (lowAfter > lowBefore * 0.7f);    // barely touched
    REQUIRE (highAfter < highBefore * 0.05f); // gone
}

TEST_CASE ("a highpass does the opposite", "[effects][dsp]")
{
    Params params { EffectType::filter };
    params.setMode (FilterMode::highpass);
    params.set (ids::cutoff, 2000.0f);

    auto low = sineBuffer (80.0, 22050);
    auto high = sineBuffer (8000.0, 22050);

    const auto lowBefore = rmsOf (low, 11025, 11025);
    const auto highBefore = rmsOf (high, 11025, 11025);

    runEffect (params, low);
    runEffect (params, high);

    REQUIRE (rmsOf (low, 11025, 11025) < lowBefore * 0.05f);
    REQUIRE (rmsOf (high, 11025, 11025) > highBefore * 0.7f);
}

TEST_CASE ("a delay produces a repeat at the time it was set to", "[effects][dsp]")
{
    Params params { EffectType::delay };
    params.set (ids::delayMs, 200.0f);
    params.set (ids::feedback, 0.0f); // one repeat only, so the position is unambiguous
    params.set (ids::mix, 1.0f);

    // A short click at the very start, then silence.
    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.0));
    buffer.clear();

    for (int i = 0; i < 64; ++i)
    {
        buffer.setSample (0, i, 0.8f);
        buffer.setSample (1, i, 0.8f);
    }

    runEffect (params, buffer);

    // Find where the energy actually landed.
    int loudest = -1;
    float loudestValue = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto value = std::abs (buffer.getSample (0, i));

        if (value > loudestValue)
        {
            loudestValue = value;
            loudest = i;
        }
    }

    const auto expected = (int) (0.200 * sampleRate);

    INFO ("peak at sample " << loudest << ", expected near " << expected);
    REQUIRE (loudestValue > 0.5f);
    REQUIRE (std::abs (loudest - expected) < 100);
}

TEST_CASE ("delay feedback makes repeats that decay rather than one or forever", "[effects][dsp]")
{
    Params params { EffectType::delay };
    params.set (ids::delayMs, 100.0f);
    params.set (ids::feedback, 0.6f);

    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.0));
    buffer.clear();

    for (int i = 0; i < 64; ++i)
        for (int c = 0; c < 2; ++c)
            buffer.setSample (c, i, 0.8f);

    runEffect (params, buffer);

    const auto window = (int) (sampleRate * 0.05);
    const auto first = rmsOf (buffer, (int) (sampleRate * 0.100) - window / 2, window);
    const auto second = rmsOf (buffer, (int) (sampleRate * 0.200) - window / 2, window);
    const auto third = rmsOf (buffer, (int) (sampleRate * 0.300) - window / 2, window);

    INFO ("repeats: " << first << " " << second << " " << third);

    REQUIRE (first > 0.01f);
    REQUIRE (second > 0.001f);
    REQUIRE (second < first);
    REQUIRE (third < second);
}

TEST_CASE ("reverb extends a sound past where it ended", "[effects][dsp]")
{
    Params params { EffectType::reverb };
    params.set (ids::roomSize, 0.85f);
    params.set (ids::damping, 0.2f);
    params.set (ids::mix, 1.0f);

    // A quarter second of tone, then silence.
    juce::AudioBuffer<float> buffer (2, (int) (sampleRate * 1.5));
    buffer.clear();

    auto tone = sineBuffer (440.0, (int) (sampleRate * 0.25));

    for (int c = 0; c < 2; ++c)
        buffer.copyFrom (c, 0, tone, c, 0, tone.getNumSamples());

    const auto tailBefore = rmsOf (buffer, (int) (sampleRate * 0.4), (int) (sampleRate * 0.2));
    REQUIRE (tailBefore < 1.0e-6f); // silent before the reverb

    runEffect (params, buffer);

    const auto tailAfter = rmsOf (buffer, (int) (sampleRate * 0.4), (int) (sampleRate * 0.2));
    const auto laterTail = rmsOf (buffer, (int) (sampleRate * 1.0), (int) (sampleRate * 0.2));

    INFO ("tail " << tailAfter << ", later " << laterTail);

    // There is now sound where there was none, and it is decaying.
    REQUIRE (tailAfter > 0.002f);
    REQUIRE (laterTail < tailAfter);
}

TEST_CASE ("drive adds harmonics rather than only volume", "[effects][dsp]")
{
    Params params { EffectType::drive };
    params.set (ids::drive, 20.0f);
    params.set (ids::outputGain, 1.0f);

    auto buffer = sineBuffer (200.0, 8192, 0.5f);
    const auto before = buffer;

    runEffect (params, buffer);

    // A hard-driven sine becomes something square-ish: its peak sits far closer
    // to its RMS than a sine's does. Comparing crest factors says "the shape
    // changed" in a way that a gain change alone cannot fake.
    const auto crest = [] (const juce::AudioBuffer<float>& b)
    {
        const auto rms = b.getRMSLevel (0, 0, b.getNumSamples());
        return rms > 0.0f ? b.getMagnitude (0, 0, b.getNumSamples()) / rms : 0.0f;
    };

    INFO ("crest before " << crest (before) << " after " << crest (buffer));
    REQUIRE (crest (buffer) < crest (before) * 0.9f);
    REQUIRE (buffer.getMagnitude (0, 0, buffer.getNumSamples()) <= 1.05f);
}

TEST_CASE ("the EQ bands move the frequencies they name", "[effects][dsp]")
{
    const auto gainAt = [] (double frequency, const Params& params)
    {
        auto buffer = sineBuffer (frequency, 22050);
        const auto before = rmsOf (buffer, 11025, 11025);
        runEffect (params, buffer);
        return rmsOf (buffer, 11025, 11025) / juce::jmax (1.0e-9f, before);
    };

    Params flat { EffectType::eq };
    REQUIRE_THAT (gainAt (100.0, flat), WithinAbs (1.0, 0.05));
    REQUIRE_THAT (gainAt (900.0, flat), WithinAbs (1.0, 0.05));
    REQUIRE_THAT (gainAt (8000.0, flat), WithinAbs (1.0, 0.05));

    Params boostLow { EffectType::eq };
    boostLow.set (ids::lowGainDb, 12.0f);
    REQUIRE (gainAt (60.0, boostLow) > 2.5f); // roughly +12dB
    REQUIRE_THAT (gainAt (8000.0, boostLow), WithinAbs (1.0, 0.1));

    Params cutMid { EffectType::eq };
    cutMid.set (ids::midGainDb, -18.0f).set (ids::midFreq, 900.0f);
    REQUIRE (gainAt (900.0, cutMid) < 0.3f);
    REQUIRE (gainAt (60.0, cutMid) > 0.9f);

    Params boostHigh { EffectType::eq };
    boostHigh.set (ids::highGainDb, 12.0f);
    REQUIRE (gainAt (12000.0, boostHigh) > 2.5f);
    REQUIRE_THAT (gainAt (60.0, boostHigh), WithinAbs (1.0, 0.1));
}

TEST_CASE ("chorus modulates rather than passing the signal through", "[effects][dsp]")
{
    Params params { EffectType::chorus };
    params.set (ids::rate, 3.0f);
    params.set (ids::depth, 0.8f);
    params.set (ids::mix, 1.0f);

    auto buffer = sineBuffer (440.0, (int) (sampleRate * 1.0));
    const auto before = buffer;

    runEffect (params, buffer);

    // Compare the second half, past the chorus's own delay: a modulated copy
    // differs from the original sample by sample even though its level is
    // similar. Identical output would mean the effect did nothing.
    double difference = 0.0;
    const auto start = buffer.getNumSamples() / 2;

    for (int i = start; i < buffer.getNumSamples(); ++i)
        difference += std::abs (buffer.getSample (0, i) - before.getSample (0, i));

    difference /= (double) (buffer.getNumSamples() - start);

    INFO ("mean difference " << difference);
    REQUIRE (difference > 0.02);
    REQUIRE (buffer.getRMSLevel (0, start, buffer.getNumSamples() - start) > 0.1f);
}

TEST_CASE ("a fully dry slot leaves the signal exactly as it was", "[effects][dsp]")
{
    Params params { EffectType::filter };
    params.set (ids::mix, 0.0f);
    params.set (ids::cutoff, 100.0f);

    auto buffer = sineBuffer (5000.0, 4096);
    const auto before = buffer;

    runEffect (params, buffer);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (buffer.getSample (0, i), before.getSample (0, i)));
}

TEST_CASE ("mix blends between dry and wet", "[effects][dsp]")
{
    const auto highContentAt = [] (float mix)
    {
        Params params { EffectType::filter };
        params.set (ids::mix, mix);
        params.set (ids::cutoff, 200.0f);

        auto buffer = sineBuffer (5000.0, 22050);
        runEffect (params, buffer);
        return rmsOf (buffer, 11025, 11025);
    };

    const auto dry = highContentAt (0.0f);
    const auto half = highContentAt (0.5f);
    const auto wet = highContentAt (1.0f);

    INFO ("dry " << dry << " half " << half << " wet " << wet);
    REQUIRE (half < dry * 0.7f);
    REQUIRE (half > wet);
}
