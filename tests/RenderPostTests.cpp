#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/OfflineRenderer.h"
#include "engine/RenderPost.h"
#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** A steady tone, so a gain change is visible without an engine. */
juce::AudioBuffer<float> tone (int numSamples, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer (2, numSamples);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (
                channel, i,
                amplitude
                    * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));

    return buffer;
}

} // namespace

// --- fades -------------------------------------------------------------------

TEST_CASE ("a fade starts at silence and reaches unity", "[render][post]")
{
    auto buffer = tone (44100);
    const auto before = buffer;

    RenderPost::applyFades (buffer, 44100.0, 0.1, 0.0);

    REQUIRE (buffer.getSample (0, 0) == Approx (0.0f).margin (1.0e-6f));

    // Past the fade, untouched.
    const auto after = (int) (0.1 * 44100.0) + 64;
    REQUIRE (buffer.getSample (0, after) == Approx (before.getSample (0, after)));
}

TEST_CASE ("a fade out ends at silence", "[render][post]")
{
    auto buffer = tone (44100);

    RenderPost::applyFades (buffer, 44100.0, 0.0, 0.1);

    REQUIRE (buffer.getSample (0, buffer.getNumSamples() - 1) == Approx (0.0f).margin (1.0e-4f));
    REQUIRE (buffer.getSample (0, 0) == Approx (tone (44100).getSample (0, 0)));
}

TEST_CASE ("fades longer than the buffer meet in the middle instead of cancelling",
           "[render][post]")
{
    auto buffer = tone (1000);

    // Ten seconds of fade into a buffer of a fiftieth of a second.
    RenderPost::applyFades (buffer, 44100.0, 10.0, 10.0);

    // Clamped to half each, so the midpoint still carries signal.
    REQUIRE (buffer.getMagnitude (0, buffer.getNumSamples()) > 0.0f);
}

// --- normalize ---------------------------------------------------------------

TEST_CASE ("normalize puts the peak exactly on target", "[render][post]")
{
    auto buffer = tone (4096, 0.25f);

    const auto target = juce::Decibels::decibelsToGain (-1.0f);
    const auto applied = RenderPost::normalize (buffer, target);

    REQUIRE (buffer.getMagnitude (0, buffer.getNumSamples()) == Approx (target).epsilon (0.001));
    REQUIRE (applied > 0.0f);
}

TEST_CASE ("normalize is a pure gain: it does not change the shape", "[render][post]")
{
    auto buffer = tone (4096, 0.25f);
    const auto before = buffer;

    RenderPost::normalize (buffer, 0.9f);

    // Every sample scaled by the same factor.
    const auto ratio = buffer.getSample (0, 100) / before.getSample (0, 100);

    for (int i = 1; i < 500; ++i)
        REQUIRE (buffer.getSample (0, i)
                 == Approx (before.getSample (0, i) * ratio).margin (1.0e-6f));
}

TEST_CASE ("normalize leaves silence alone rather than dividing by nothing", "[render][post]")
{
    juce::AudioBuffer<float> buffer (2, 1024);
    buffer.clear();

    REQUIRE (RenderPost::normalize (buffer, 0.9f) == Approx (0.0f));
    REQUIRE (buffer.getMagnitude (0, 1024) == Approx (0.0f));
}

TEST_CASE ("normalize will not amplify a near-silent render past its cap", "[render][post]")
{
    auto buffer = tone (4096, 1.0e-6f);

    const auto applied = RenderPost::normalize (buffer, 1.0f, 20.0f);

    // Reaching 1.0 from here needs 120dB. The cap says no.
    REQUIRE (applied == Approx (20.0f).margin (0.01f));
    REQUIRE (buffer.getMagnitude (0, 4096) < 1.0f);
}

// --- dither ------------------------------------------------------------------

TEST_CASE ("dither puts noise into digital silence, bounded by one LSB", "[render][post]")
{
    juce::AudioBuffer<float> buffer (2, 4096);
    buffer.clear();

    RenderPost::dither (buffer, 16, 1234);

    const auto lsb = 2.0f / (float) (1 << 16);
    const auto peak = buffer.getMagnitude (0, 4096);

    REQUIRE (peak > 0.0f);
    REQUIRE (peak <= lsb);

    // Triangular and centred: the mean should sit on zero.
    double sum = 0.0;
    for (int i = 0; i < 4096; ++i)
        sum += buffer.getSample (0, i);

    REQUIRE (std::abs (sum / 4096.0) < (double) lsb * 0.1);
}

TEST_CASE ("dither is reproducible for a given seed", "[render][post]")
{
    juce::AudioBuffer<float> a (2, 512), b (2, 512), c (2, 512);
    a.clear();
    b.clear();
    c.clear();

    RenderPost::dither (a, 16, 99);
    RenderPost::dither (b, 16, 99);
    RenderPost::dither (c, 16, 100);

    for (int i = 0; i < 512; ++i)
        REQUIRE (juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i)));

    // A different seed is a different noise, or the seed is not doing anything.
    bool anyDifferent = false;
    for (int i = 0; i < 512; ++i)
        anyDifferent = anyDifferent
                       || ! juce::exactlyEqual (a.getSample (0, i), c.getSample (0, i));

    REQUIRE (anyDifferent);
}

TEST_CASE ("dither does nothing above sixteen bits", "[render][post]")
{
    juce::AudioBuffer<float> buffer (2, 1024);
    buffer.clear();

    RenderPost::dither (buffer, 24, 1);
    REQUIRE (buffer.getMagnitude (0, 1024) == Approx (0.0f));

    RenderPost::dither (buffer, 32, 1);
    REQUIRE (buffer.getMagnitude (0, 1024) == Approx (0.0f));
}

TEST_CASE ("dither cannot push a normalized peak over full scale", "[render][post]")
{
    juce::AudioBuffer<float> buffer (2, 256);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 256; ++i)
            buffer.setSample (channel, i, 1.0f);

    RenderPost::dither (buffer, 16, 7);

    // The writer clips at +/-1.0, and a clipped loudest sample is a click.
    REQUIRE (buffer.getMagnitude (0, 256) < 1.0f);
}

// --- through the renderer ----------------------------------------------------

TEST_CASE ("normalizing a render lifts its peak to the asked-for level", "[engine][render][post]")
{
    const auto project = dew::testing::fixtureProject();

    juce::AudioBuffer<float> plain;
    const auto before = OfflineRenderer::renderToBuffer (project, plain, {});

    RenderOptions options;
    options.normalize = true;
    options.normalizePeakDb = -1.0f;

    juce::AudioBuffer<float> normalized;
    const auto after = OfflineRenderer::renderToBuffer (project, normalized, options);

    REQUIRE (before.ok());
    REQUIRE (after.ok());
    REQUIRE (after.peak == Approx (juce::Decibels::decibelsToGain (-1.0f)).epsilon (0.001));
    REQUIRE (after.normalizationGainDb > 0.0f);
}

TEST_CASE ("a render fades from silence when asked", "[engine][render][post]")
{
    RenderOptions options;
    options.fadeInSeconds = 0.05;

    juce::AudioBuffer<float> rendered;
    const auto report = OfflineRenderer::renderToBuffer (dew::testing::fixtureProject(), rendered,
                                                         options);

    REQUIRE (report.ok());
    REQUIRE (rendered.getSample (0, 0) == Approx (0.0f).margin (1.0e-6f));
}

TEST_CASE ("renderToBuffer never dithers", "[engine][render][post]")
{
    // The default has dither on, but a float buffer has no LSB to dither
    // against, so two renders must still be identical.
    juce::AudioBuffer<float> a, b;

    REQUIRE (OfflineRenderer::renderToBuffer (dew::testing::fixtureProject(), a, {}).ok());
    REQUIRE (OfflineRenderer::renderToBuffer (dew::testing::fixtureProject(), b, {}).ok());

    REQUIRE (a.getNumSamples() == b.getNumSamples());

    for (int i = 0; i < a.getNumSamples(); ++i)
        REQUIRE (juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i)));
}
