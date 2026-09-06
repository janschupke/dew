// The tables themselves: their frames, their mips, and how they are addressed.
//
// Split out of a WavetableTests.cpp that was 1,046 lines. It already carried
// three fixture blocks, one immediately before each group of tags, so each
// file takes its own with it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/SynthChannel.h"
#include "engine/Wavetable.h"
#include "model/AutomationTargets.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSerializer.h"
#include "ui/OscillatorSection.h"

#include "FixtureProject.h"

using namespace dew;
using Catch::Approx;

namespace
{

/** The amplitude of one harmonic of a frame, by direct correlation.

    Deliberately not another FFT: these tests exist to pin the convention the
    generator's FFT uses, and checking it with the same transform would agree
    with itself whatever it did.
*/
double harmonicAmplitude (const Wavetable& table, float position, int mip, int k)
{
    const auto n = wavetableMipSize (mip);

    double re = 0.0, im = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto value = (double) table.at (position, mip, (double) i / (double) n);
        const auto theta = juce::MathConstants<double>::twoPi * (double) k * (double) i
                           / (double) n;
        re += value * std::cos (theta);
        im += value * std::sin (theta);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

const Wavetable& tableNamed (const juce::String& name)
{
    const auto index = wavetableIndexFor (name);
    REQUIRE (index >= 0);
    return wavetableAt (index);
}

} // namespace

// --- the convention ----------------------------------------------------------

TEST_CASE ("the first frame of the basic table is a unit sine", "[wavetable]")
{
    // The test the whole generator rests on. It pins three things at once: that
    // a harmonic amplitude of 1 comes out at unit level, that bin k is a SINE
    // and not a cosine, and that the sign is positive. A saw reconstructed from
    // cosines is not a saw, and nothing downstream would say so.
    const auto& basic = tableNamed ("basic");

    for (int i = 0; i < kWavetableSize; ++i)
    {
        const auto phase = (double) i / (double) kWavetableSize;
        const auto expected = std::sin (juce::MathConstants<double>::twoPi * phase);

        INFO ("sample " << i);
        REQUIRE ((double) basic.at (0.0f, 0, phase) == Approx (expected).margin (1.0e-4));
    }
}

TEST_CASE ("the last frame of the basic table is a saw", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    // A saw's harmonics fall as 1/k. Normalisation scales them all together, so
    // the RATIOS are what identify the shape.
    const auto first = harmonicAmplitude (basic, 1.0f, 0, 1);

    REQUIRE (first > 0.1);
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 2) == Approx (first / 2.0).epsilon (0.05));
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 3) == Approx (first / 3.0).epsilon (0.05));

    // Every harmonic present, which is what distinguishes a saw from a square.
    REQUIRE (harmonicAmplitude (basic, 1.0f, 0, 500) > 0.0);
}

TEST_CASE ("a mip carries the same levels as the one above it", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    // Every mip is scaled by mip 0's peak rather than by its own. If it were
    // normalised per mip, a held note would change level as it rose through a
    // mip boundary - and this is the only place that would catch it.
    const auto reference = harmonicAmplitude (basic, 1.0f, 0, 1);

    for (int mip = 1; mip <= 5; ++mip)
    {
        INFO ("mip " << mip);
        REQUIRE (harmonicAmplitude (basic, 1.0f, mip, 1) == Approx (reference).epsilon (0.02));
    }
}

// --- the bank ----------------------------------------------------------------

TEST_CASE ("every frame of every table is finite and bounded", "[wavetable]")
{
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);
        INFO ("table " << table.getName());

        for (int frame = 0; frame < kWavetableFrames; ++frame)
        {
            for (int mip = 0; mip < kWavetableMips; ++mip)
            {
                const auto n = wavetableMipSize (mip);
                const auto* samples = table.frameData (frame, mip);

                for (int i = 0; i <= n; ++i)
                {
                    INFO ("frame " << frame << " mip " << mip << " sample " << i);
                    REQUIRE (std::isfinite (samples[i]));

                    // Normalisation makes mip 0 peak at exactly 1; the smaller
                    // mips are allowed to overshoot it. Truncating a series
                    // rings around the shape it is approximating, and the
                    // smallest mips - four harmonics over eight samples - ring
                    // hardest. Measured worst case is 1.21, on `fold`. Clamping
                    // it away would cost a mip its level agreement with the
                    // others, which is the one thing that has to hold.
                    REQUIRE (std::abs (samples[i]) <= 1.25f);
                }

                // The guard sample is what lets interpolation skip the modulo.
                REQUIRE (juce::exactlyEqual (samples[n], samples[0]));
            }
        }
    }
}

TEST_CASE ("a frame reaches full scale", "[wavetable]")
{
    // Normalisation divides by mip 0's peak, so every frame should touch 1.
    // A frame that came out quiet would be one whose spectrum is empty.
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);

        for (int frame = 0; frame < kWavetableFrames; ++frame)
        {
            float peak = 0.0f;
            const auto* samples = table.frameData (frame, 0);

            for (int i = 0; i < kWavetableSize; ++i)
                peak = juce::jmax (peak, std::abs (samples[i]));

            INFO (table.getName() << " frame " << frame);
            REQUIRE (peak == Approx (1.0f).margin (0.001));
        }
    }
}

TEST_CASE ("the tables differ from each other and across their frames", "[wavetable]")
{
    // A generator that ignored its frame index, or its spectrum function, would
    // still pass every test above.
    for (int t = 0; t < wavetableCount(); ++t)
    {
        const auto& table = wavetableAt (t);
        INFO (table.getName());

        bool movedAcrossFrames = false;

        for (int i = 0; i < kWavetableSize && ! movedAcrossFrames; ++i)
            movedAcrossFrames = ! juce::exactlyEqual (table.frameData (0, 0)[i],
                                                      table.frameData (kWavetableFrames - 1, 0)[i]);

        REQUIRE (movedAcrossFrames);
    }

    for (int a = 0; a < wavetableCount(); ++a)
    {
        for (int b = a + 1; b < wavetableCount(); ++b)
        {
            bool differ = false;

            for (int i = 0; i < kWavetableSize && ! differ; ++i)
                differ = ! juce::exactlyEqual (wavetableAt (a).frameData (8, 0)[i],
                                               wavetableAt (b).frameData (8, 0)[i]);

            INFO (wavetableAt (a).getName() << " vs " << wavetableAt (b).getName());
            REQUIRE (differ);
        }
    }
}

TEST_CASE ("position crossfades between neighbouring frames", "[wavetable]")
{
    const auto& basic = tableNamed ("basic");

    const auto atFrame = [&basic] (int frame, double phase)
    { return (double) basic.frameData (frame, 0)[(int) (phase * kWavetableSize)]; };

    // Landing exactly on a sample, so this measures the frame crossfade and
    // not the phase interpolation either side of it.
    const auto phase = 614.0 / (double) kWavetableSize;

    // Exactly on a frame, and exactly between two.
    REQUIRE ((double) basic.at (0.0f, 0, phase) == Approx (atFrame (0, phase)).margin (1.0e-5));
    REQUIRE ((double) basic.at (1.0f, 0, phase)
             == Approx (atFrame (kWavetableFrames - 1, phase)).margin (1.0e-5));

    const auto halfway = 0.5f / (float) (kWavetableFrames - 1);
    REQUIRE ((double) basic.at (halfway, 0, phase)
             == Approx ((atFrame (0, phase) + atFrame (1, phase)) / 2.0).margin (1.0e-5));
}

// --- the bank's names --------------------------------------------------------

TEST_CASE ("a table is addressed by name, and an unknown one is refused", "[wavetable]")
{
    REQUIRE (wavetableCount() > 0);

    for (int i = 0; i < wavetableCount(); ++i)
    {
        INFO ("index " << i);
        REQUIRE (wavetableIndexFor (wavetableAt (i).getName()) == i);
        REQUIRE (tr (wavetableAt (i).getDisplayName()).isNotEmpty());
    }

    // -1 rather than 0: a name the build does not know is a fault in the file,
    // and the caller has to be able to say so rather than silently sound wrong.
    REQUIRE (wavetableIndexFor ("nothing-like-this") == -1);

    // Out of range clamps, so no caller can index off the end of the bank.
    REQUIRE (wavetableAt (-5).getName() == wavetableAt (0).getName());
    REQUIRE (wavetableAt (9999).getName() == wavetableAt (wavetableCount() - 1).getName());
}

TEST_CASE ("the mip for a phase increment stays band-limited", "[wavetable]")
{
    REQUIRE (wavetableMipFor (0.0) == 0);
    REQUIRE (wavetableMipFor (-1.0) == 0);

    for (double dt = 1.0e-5; dt < 0.5; dt *= 1.3)
    {
        const auto mip = wavetableMipFor (dt);

        INFO ("dt " << dt << " mip " << mip);
        REQUIRE (mip >= 0);
        REQUIRE (mip < kWavetableMips);

        // The whole point: the highest harmonic this mip carries must still sit
        // below Nyquist at this pitch.
        REQUIRE ((double) wavetableMipHarmonics (mip) * dt <= 0.5 + 1.0e-9);
    }
}
