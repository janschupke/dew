#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "app/ProjectDocument.h"
#include "model/ProjectFactory.h"
#include "ui/design/DewLookAndFeel.h"
#include "ui/EditorState.h"
#include "ui/design/SignalScope.h"
#include "ui/TransportBar.h"
#include "ui/design/Tokens.h"
#include "PaintProbe.h"

using namespace dew::testing;

using namespace dew;
using Catch::Approx;

namespace
{




/** How much of `area`'s height the accent-coloured pixels span. A trace drawn at
    the wrong scale still puts pixels on screen; this says how tall it got.
*/
float verticalSpanOfAccent (const juce::Image& image, juce::Rectangle<int> area)
{
    auto top = area.getBottom(), bottom = area.getY();

    for (int y = area.getY(); y < area.getBottom(); ++y)
    {
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);

            if (std::abs ((int) pixel.getRed() - (int) tokens::colour::accent.getRed()) < 24
                && std::abs ((int) pixel.getGreen() - (int) tokens::colour::accent.getGreen()) < 24
                && std::abs ((int) pixel.getBlue() - (int) tokens::colour::accent.getBlue()) < 24
                && pixel.getAlpha() > 200)
            {
                top = juce::jmin (top, y);
                bottom = juce::jmax (bottom, y);
            }
        }
    }

    return bottom < top ? 0.0f : (float) (bottom - top + 1) / (float) area.getHeight();
}

bool regionsMatch (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;

    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                return false;

    return true;
}

bool imagesMatch (const juce::Image& a, const juce::Image& b)
{
    return regionsMatch (a, b, a.getBounds());
}

std::vector<float> sine (double frequency, double sampleRate, float amplitude = 0.8f,
                         double phase = 0.0)
{
    std::vector<float> samples ((size_t) SignalScope::windowSamples, 0.0f);

    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                       * frequency * (double) i / sampleRate
                                                   + phase);

    return samples;
}

std::vector<float> silence()
{
    return std::vector<float> ((size_t) SignalScope::windowSamples, 0.0f);
}

/** A sized, visible scope. A struct rather than a factory because the widget is
    non-copyable, like every other juce::Component.
*/
struct ScopeHarness
{
    explicit ScopeHarness (int width = SignalScope::preferredWidth, int height = 26)
    {
        scope.setSize (width, height);
        scope.setVisible (true);
    }

    SignalScope scope;
};

int loudestBand (const SignalScope& scope)
{
    auto best = 0;

    for (int band = 1; band < scope.getNumBands(); ++band)
        if (scope.getBandLevel (band) > scope.getBandLevel (best))
            best = band;

    return best;
}

} // namespace

TEST_CASE ("a scope with no engine paints its idle state", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SignalScope scope;
    scope.setSize (SignalScope::preferredWidth, 26);
    scope.setVisible (true);

    REQUIRE (scope.isIdle());

    const auto image = render (scope);

    // The wells and their baselines are there - empty is not blank.
    REQUIRE (inkCoverage (image) > 0.0f);

    // But nothing is drawn in them.
    REQUIRE (coverageOf (image, tokens::colour::accent) < 0.005f);
}

TEST_CASE ("a signal draws a trace and bars", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ScopeHarness harness;
    auto& scope = harness.scope;
    const auto idleImage = render (scope);

    REQUIRE (juce::exactlyEqual (coverageOf (idleImage, tokens::colour::accent), 0.0f));

    const auto tone = sine (220.0, 44100.0, 0.8f);
    scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);

    REQUIRE (! scope.isIdle());

    const auto liveImage = render (scope);

    // Both wells said something.
    REQUIRE (verticalSpanOfAccent (liveImage, scope.getScopeBounds()) > 0.0f);
    REQUIRE (verticalSpanOfAccent (liveImage, scope.getSpectrumBounds()) > 0.0f);

    // And at the right scale rather than merely on screen: a sine at eight
    // tenths of full scale has to use most of the well, top to bottom. A pixel
    // count would pass just as happily on a flat line in the wrong place.
    REQUIRE (verticalSpanOfAccent (liveImage, scope.getScopeBounds()) > 0.7f);
}

TEST_CASE ("a sine puts its energy in the band that contains its frequency", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Three widely separated tones, so this cannot pass by always answering the
    // same band.
    for (const auto frequency : { 100.0, 1000.0, 8000.0 })
    {
        SignalScope scope;
        const auto tone = sine (frequency, 44100.0);
        scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);

        const auto peak = loudestBand (scope);

        INFO ("tone " << frequency << "Hz, loudest band " << peak
                      << " (" << scope.getBandLowHz (peak) << ".."
                      << scope.getBandHighHz (peak) << "Hz)");

        REQUIRE (scope.getBandLowHz (peak) <= frequency);
        REQUIRE (frequency < scope.getBandHighHz (peak));

        // And the energy is IN that band rather than smeared across the axis,
        // which is what makes this a test of the window and the bin mapping
        // rather than of picking a maximum.
        std::vector<float> levels;

        for (int band = 0; band < scope.getNumBands(); ++band)
            if (band != peak)
                levels.push_back (scope.getBandLevel (band));

        std::sort (levels.begin(), levels.end());
        const auto median = levels[levels.size() / 2];

        REQUIRE (juce::Decibels::gainToDecibels (scope.getBandLevel (peak), -120.0f)
                     - juce::Decibels::gainToDecibels (median, -120.0f) > 20.0f);
    }
}

TEST_CASE ("the spectrum follows the signal's level", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dbOfTone = [] (float amplitude)
    {
        SignalScope scope;
        const auto tone = sine (1000.0, 44100.0, amplitude);
        scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);
        return scope.getBandDb (loudestBand (scope));
    };

    // A tenth of the amplitude is twenty decibels down, and it has to read that
    // way or the floor above means nothing.
    REQUIRE (dbOfTone (1.0f) - dbOfTone (0.1f) == Approx (20.0f).margin (3.0f));
}

TEST_CASE ("a full-scale sine reads as full scale", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The calibration everything else hangs off: 0dBFS in, 0dB out.
    //
    // On a bin centre (48 bins up at 44100/2048 per bin), because a tone BETWEEN
    // two bins reads up to 1.4dB low through a Hann window and that scalloping
    // loss would be indistinguishable from a wrong scale factor.
    SignalScope scope;
    const auto tone = sine (44100.0 * 48.0 / SignalScope::windowSamples, 44100.0, 1.0f);
    scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);

    REQUIRE (scope.getBandDb (loudestBand (scope)) == Approx (0.0f).margin (0.5f));
}

TEST_CASE ("the trace does not depend on where the window starts", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The whole point of the trigger. Two windows of the same tone, one shifted
    // by a quarter cycle, have to draw the same picture.
    //
    // 441Hz at 44100 is a hundred samples to the cycle, so the quarter-cycle
    // shift is exactly twenty-five samples. That matters: the trigger aligns to
    // a SAMPLE, not to a sub-sample zero crossing, so a shift that is not a
    // whole number of samples would land the trace on a different fraction of a
    // cycle and legitimately differ by a pixel here and there.
    ScopeHarness firstHarness, secondHarness;
    auto& first = firstHarness.scope;
    auto& second = secondHarness.scope;

    // Both carry the same small phase offset so that no SAMPLE lands exactly on
    // a zero. A mathematical zero computes to plus or minus 1e-16, and which
    // side of zero it falls on decides whether the crossing is detected at that
    // sample or the next - an ambiguity in the test signal, not in the trigger,
    // and one no real audio has.
    constexpr double offset = 0.3;

    const auto aligned = sine (441.0, 44100.0, 0.8f, offset);
    const auto shifted = sine (441.0, 44100.0, 0.8f,
                               offset + juce::MathConstants<double>::halfPi);

    first.pushFrame (aligned.data(), (int) aligned.size(), 44100.0);
    second.pushFrame (shifted.data(), (int) shifted.size(), 44100.0);

    REQUIRE (first.isTriggered());
    REQUIRE (second.isTriggered());

    // Without this the test could pass because the two inputs were the same.
    REQUIRE (first.getTriggerOffset() != second.getTriggerOffset());

    // The trigger removed the shift exactly.
    REQUIRE (first.getTriggerOffset() - second.getTriggerOffset() == 25);

    // Compared over the scope's well and not the whole widget, because the
    // trigger governs the trace and nothing else: the spectrum beside it is
    // analysing a genuinely different slice of the tone, and its bars are
    // entitled to differ by a pixel.
    REQUIRE (regionsMatch (render (first), render (second), first.getScopeBounds()));
}

TEST_CASE ("a signal with no zero crossing still draws", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ScopeHarness harness;
    auto& scope = harness.scope;

    // Riding a DC offset: never crosses zero, so there is nothing to trigger on.
    auto offset = sine (300.0, 44100.0, 0.1f);

    for (auto& sample : offset)
        sample += 0.5f;

    scope.pushFrame (offset.data(), (int) offset.size(), 44100.0);

    REQUIRE (! scope.isTriggered());
    REQUIRE (! scope.isIdle());

    // Untriggered, but still a picture rather than an empty well.
    REQUIRE (coverageOf (render (scope), tokens::colour::accent) > 0.0f);
}

TEST_CASE ("silence returns the display to idle", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ScopeHarness harness;
    auto& scope = harness.scope;
    const auto idleImage = render (scope);

    const auto tone = sine (1000.0, 44100.0);
    scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);
    REQUIRE (! scope.isIdle());

    // The ballistics have to run DOWN as well as up: idle must be reachable, not
    // only an initial condition.
    const auto quiet = silence();

    for (int i = 0; i < 200; ++i)
        scope.pushFrame (quiet.data(), (int) quiet.size(), 44100.0);

    REQUIRE (scope.isIdle());

    for (int band = 0; band < scope.getNumBands(); ++band)
        REQUIRE (juce::exactlyEqual (scope.getBandLevel (band), 0.0f));

    REQUIRE (imagesMatch (render (scope), idleImage));
}

TEST_CASE ("the wells do not move when sound starts", "[signalscope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ScopeHarness harness;
    auto& scope = harness.scope;

    const auto scopeWell = scope.getScopeBounds();
    const auto spectrumWell = scope.getSpectrumBounds();

    REQUIRE (! scopeWell.isEmpty());
    REQUIRE (! spectrumWell.isEmpty());

    const auto tone = sine (440.0, 44100.0);
    scope.pushFrame (tone.data(), (int) tone.size(), 44100.0);

    REQUIRE (scope.getScopeBounds() == scopeWell);
    REQUIRE (scope.getSpectrumBounds() == spectrumWell);
}

// --- the transport bar -------------------------------------------------------

namespace
{

struct BarHarness
{
    BarHarness()
    {
        document.setState (ProjectFactory::createDefault(), true);
        bar.setLookAndFeel (&lookAndFeel);
        bar.setVisible (true);
    }

    ~BarHarness() { bar.setLookAndFeel (nullptr); }

    juce::Component* findScope()
    {
        for (auto* child : bar.getChildren())
            if (child->getComponentID() == "signalScope")
                return child;

        return nullptr;
    }

    DewLookAndFeel lookAndFeel;
    ProjectDocument document;
    AudioEngine engine;
    EditorState editorState;
    TransportBar bar { document, engine, editorState };
};

} // namespace

TEST_CASE ("the transport bar shows the visualiser when there is room", "[signalscope][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BarHarness h;
    h.bar.setSize (1400, 46);

    auto* scope = h.findScope();
    REQUIRE (scope != nullptr);
    REQUIRE (scope->isVisible());
    REQUIRE (scope->getWidth() == SignalScope::preferredWidth);
    REQUIRE (h.bar.getLocalBounds().contains (scope->getBounds()));

    // On the same line as everything else in the bar.
    for (auto* child : h.bar.getChildren())
        if (child != scope && child->isVisible() && ! child->getBounds().isEmpty())
            REQUIRE (child->getY() == scope->getY());
}

TEST_CASE ("narrowing the bar hides the visualiser and damages nothing else",
           "[signalscope][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BarHarness h;
    h.bar.setSize (1400, 46);

    auto* scope = h.findScope();
    REQUIRE (scope != nullptr);

    juce::Array<juce::Rectangle<int>> before;

    for (auto* child : h.bar.getChildren())
        if (child != scope)
            before.add (child->getBounds());

    h.bar.setSize (900, 46);

    REQUIRE (! scope->isVisible());

    // The visualiser is additive: it must never cost the controls that were
    // already there a single pixel.
    int index = 0;

    for (auto* child : h.bar.getChildren())
        if (child != scope)
            REQUIRE (child->getBounds() == before[index++]);
}

TEST_CASE ("the visualiser never overlaps another control", "[signalscope][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BarHarness h;

    for (const auto width : { 1010, 1180, 1400, 2400 })
    {
        h.bar.setSize (width, 46);

        auto* scope = h.findScope();
        REQUIRE (scope != nullptr);

        if (! scope->isVisible())
            continue;

        INFO ("bar width " << width);

        for (auto* child : h.bar.getChildren())
            if (child != scope && child->isVisible())
                REQUIRE (! child->getBounds().intersects (scope->getBounds()));
    }
}
