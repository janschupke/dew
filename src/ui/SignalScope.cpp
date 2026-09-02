#include "SignalScope.h"

#include <algorithm>
#include <cmath>

#include "engine/AudioEngine.h"

namespace dew
{

using namespace tokens;

namespace
{
/** How fast a level falls between ticks. Lifted from the mixer's meters rather
    than chosen again, so the two read as one instrument - which also means this
    widget has to run at the rate they do, because the coefficient is per tick.
*/
constexpr float fallCoefficient = 0.82f;

float applyBallistics (float current, float incoming, float floorGain) noexcept
{
    // Rise instantly, fall over about a third of a second. A display that fell
    // as fast as it rose would be unreadable on percussive material - and the
    // instant rise is also what lets a test assert a level after ONE frame.
    auto level = incoming > current ? incoming
                                    : current * fallCoefficient + incoming * (1.0f - fallCoefficient);

    return level < floorGain ? 0.0f : level;
}
} // namespace

SignalScope::SignalScope (AudioEngine* e) : engine (e)
{
    setComponentID ("signalScope");

    // A display, and nothing more. dew has already had three "this doesn't do
    // anything" reports that were one child component quietly eating a press.
    setInterceptsMouseClicks (false, false);

    rebuildBandBins (frameSampleRate);

    // Only with an engine to poll. The design gallery and the tests build this
    // without one and drive it through pushFrame, and a timer running there
    // would make a screenshot and a test depend on the clock.
    if (engine != nullptr)
        startTimerHz (motion::uiRefreshHz);
}

SignalScope::~SignalScope() = default;

// --- band geometry -----------------------------------------------------------

double SignalScope::getBandLowHz (int band) const noexcept
{
    const auto ratio = bandHighHz / bandLowHz;
    return bandLowHz * std::pow (ratio, (double) juce::jlimit (0, numBands, band) / (double) numBands);
}

double SignalScope::getBandHighHz (int band) const noexcept
{
    return getBandLowHz (band + 1);
}

double SignalScope::getBandCentreHz (int band) const noexcept
{
    // Geometric, because the axis is logarithmic: the arithmetic mean of a
    // band's edges is not in its middle on a display like this.
    return std::sqrt (getBandLowHz (band) * getBandHighHz (band));
}

float SignalScope::getBandLevel (int band) const noexcept
{
    return juce::isPositiveAndBelow (band, numBands) ? bandLevels[(size_t) band] : 0.0f;
}

float SignalScope::getBandDb (int band) const noexcept
{
    return juce::Decibels::gainToDecibels (getBandLevel (band), (float) spectrumFloorDb);
}

// --- analysis ----------------------------------------------------------------

void SignalScope::rebuildBandBins (double sampleRate)
{
    // Once per rate, not once per frame.
    if (juce::approximatelyEqual (sampleRate, binsBuiltFor))
        return;

    binsBuiltFor = sampleRate;

    const auto binHz = sampleRate / (double) windowSamples;
    const auto topBin = windowSamples / 2 - 1;

    for (int band = 0; band < numBands; ++band)
    {
        auto first = (int) std::ceil (getBandLowHz (band) / binHz);
        auto last = (int) std::floor (getBandHighHz (band) / binHz);

        // At 21.5Hz per bin the lowest bands are narrower than one bin and
        // contain no bin centre at all. Falling back to the nearest bin keeps
        // them alive; without it the bottom of the display is permanently dead.
        if (last < first)
            first = last = (int) std::lround (getBandCentreHz (band) / binHz);

        // From bin 1: bin 0 is DC, and a display that drew it would show an
        // offset as a permanent bass band.
        bandFirstBin[(size_t) band] = juce::jlimit (1, topBin, first);
        bandLastBin[(size_t) band] = juce::jlimit (bandFirstBin[(size_t) band], topBin, last);
    }
}

void SignalScope::analyseSpectrum (double sampleRate)
{
    rebuildBandBins (sampleRate);

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (window.begin(), window.end(), fftData.begin());

    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) windowSamples);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // N/2 for the transform and Hann's coherent gain of 0.5, so a full-scale
    // sine reads 1.0 in the band it lands in - which is what makes the floor
    // below mean decibels relative to full scale rather than to nothing.
    const auto scale = 1.0f / ((float) windowSamples * 0.25f);
    const auto floorGain = juce::Decibels::decibelsToGain ((float) spectrumFloorDb);

    for (int band = 0; band < numBands; ++band)
    {
        auto magnitude = 0.0f;

        // The maximum over the band's bins, not the mean. The top band spans
        // over a hundred bins, and averaging would bury a single tone in them.
        for (int bin = bandFirstBin[(size_t) band]; bin <= bandLastBin[(size_t) band]; ++bin)
            magnitude = juce::jmax (magnitude, fftData[(size_t) bin]);

        auto& level = bandLevels[(size_t) band];
        level = applyBallistics (level, magnitude * scale, floorGain);
    }
}

int SignalScope::findTrigger (int displayStart, int searchSamples, float peak) const
{
    const auto hysteresis = juce::jmax (0.02f * peak, traceFloor);

    for (int i = displayStart; i > displayStart - searchSamples && i > 0; --i)
    {
        if (! (window[(size_t) i] > 0.0f && window[(size_t) (i - 1)] <= 0.0f))
            continue;

        // Confirm it is the rising edge of a real excursion and not a wiggle on
        // the zero line: going back from the crossing the signal has to reach
        // -hysteresis before it reaches +hysteresis. Without this, dither
        // triggers at a different sample every frame and the trace shimmers -
        // which is the entire thing the trigger exists to prevent.
        for (int j = i - 1; j > i - searchSamples && j > 0; --j)
        {
            if (window[(size_t) j] < -hysteresis)
                return i;

            if (window[(size_t) j] > hysteresis)
                break;
        }
    }

    // The most recent qualifying crossing, so the trace is as fresh as it can
    // be; none at all is reported rather than guessed at.
    return -1;
}

void SignalScope::rebuildColumns()
{
    columnTop.clear();
    columnBottom.clear();

    const auto width = scopeBounds.getWidth() - 2;

    if (width <= 0 || displaySamples <= 0 || idle)
        return;

    const auto well = scopeBounds.toFloat();
    const auto centreY = well.getCentreY();

    // Linear and fixed, not auto-gained: this well is the picture of the
    // waveform and the one beside it is the dB view. Auto-gain would make
    // silence look loud and the display useless as a level.
    const auto halfHeight = juce::jmax (1.0f, well.getHeight() * 0.5f - 1.0f);

    columnTop.resize ((size_t) width);
    columnBottom.resize ((size_t) width);

    for (int x = 0; x < width; ++x)
    {
        const auto from = triggerOffset + (int) ((juce::int64) x * displaySamples / width);
        const auto to = juce::jmax (from + 1,
                                    triggerOffset + (int) ((juce::int64) (x + 1) * displaySamples / width));

        auto lowest = window[(size_t) from];
        auto highest = lowest;

        for (int i = from; i < to && i < windowSamples; ++i)
        {
            lowest = juce::jmin (lowest, window[(size_t) i]);
            highest = juce::jmax (highest, window[(size_t) i]);
        }

        auto top = centreY - juce::jlimit (-1.0f, 1.0f, highest) * halfHeight;
        auto bottom = centreY - juce::jlimit (-1.0f, 1.0f, lowest) * halfHeight;

        // A column thinner than a pixel would draw as nothing at all, so a
        // quiet-but-present signal would vanish instead of reading as quiet.
        if (bottom - top < 1.0f)
        {
            const auto middle = 0.5f * (top + bottom);
            top = middle - 0.5f;
            bottom = middle + 0.5f;
        }

        columnTop[(size_t) x] = top;
        columnBottom[(size_t) x] = bottom;
    }
}

void SignalScope::pushFrame (const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || numSamples <= 0)
        return;

    frameSampleRate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;

    const auto kept = juce::jmin (numSamples, windowSamples);
    std::fill (window.begin(), window.end(), 0.0f);
    std::copy (mono + numSamples - kept, mono + numSamples, window.begin() + (windowSamples - kept));

    auto peak = 0.0f;

    for (auto sample : window)
        peak = juce::jmax (peak, std::abs (sample));

    signalLevel = applyBallistics (signalLevel, peak, traceFloor);

    displaySamples = juce::jlimit (16, windowSamples / 3,
                                   juce::roundToInt (0.001 * displaySpanMs * frameSampleRate));

    const auto displayStart = windowSamples - displaySamples;
    const auto searchSamples = juce::jmin (displaySamples, windowSamples - displaySamples);
    const auto found = signalLevel > 0.0f ? findTrigger (displayStart, searchSamples, peak) : -1;

    triggered = found >= 0;

    // Untriggered draws the newest window instead of nothing: a shimmering
    // trace is a better answer than an empty well.
    triggerOffset = triggered ? found : displayStart;

    analyseSpectrum (frameSampleRate);

    const auto wasIdle = idle;
    auto anyBand = false;

    for (auto level : bandLevels)
        anyBand = anyBand || level > 0.0f;

    // Both halves have to be quiet, so the display cannot flicker between
    // states while one of them is still decaying.
    idle = signalLevel <= 0.0f && ! anyBand;

    rebuildColumns();

    // A silent app costs no repaints at all.
    if (idle && wasIdle)
        return;

    // The whole component, because the whole component is smaller than the
    // sub-rectangle anything else in dew bothers to invalidate.
    repaint();
}

void SignalScope::timerCallback()
{
    const auto& tap = engine->getSignalTap();
    const auto count = tap.getWriteCount();

    if (count == lastWriteCount)
    {
        // No device, or one that has stopped. Decay to idle rather than leaving
        // the last frame frozen on screen forever - "silence is arriving" and
        // "nothing is arriving" must not look the same.
        const std::array<float, 64> silence {};
        pushFrame (silence.data(), (int) silence.size(), frameSampleRate);
        return;
    }

    lastWriteCount = count;

    std::array<float, windowSamples> incoming {};

    // A refused read means the writer lapped us; keeping the frame we have is
    // the right answer, so there is no else here.
    if (tap.readLatest (incoming.data(), windowSamples))
        pushFrame (incoming.data(), windowSamples, tap.getSampleRate());
}

// --- painting ----------------------------------------------------------------

void SignalScope::resized()
{
    auto area = getLocalBounds();

    scopeBounds = area.removeFromLeft ((area.getWidth() - space::sm) / 2);
    area.removeFromLeft (space::sm);
    spectrumBounds = area;

    rebuildColumns();
}

void SignalScope::paintScope (juce::Graphics& g) const
{
    const auto well = scopeBounds.toFloat();

    g.setColour (colour::wellDeep);
    g.fillRoundedRectangle (well, radius::sm);

    // Drawn whether or not there is a signal, so the well reads as present and
    // silent rather than broken, and so nothing moves when sound starts.
    g.setColour (colour::divider);
    g.drawHorizontalLine (juce::roundToInt (well.getCentreY()),
                          well.getX() + 1.0f, well.getRight() - 1.0f);

    if (columnTop.empty())
        return;

    juce::Path trace;

    for (size_t x = 0; x < columnTop.size(); ++x)
    {
        const auto px = well.getX() + 1.0f + (float) x;

        if (x == 0)
            trace.startNewSubPath (px, columnTop[x]);
        else
            trace.lineTo (px, columnTop[x]);
    }

    for (auto x = (int) columnBottom.size() - 1; x >= 0; --x)
        trace.lineTo (well.getX() + 1.0f + (float) x, columnBottom[(size_t) x]);

    trace.closeSubPath();

    g.setColour (colour::accent);
    g.fillPath (trace);
}

void SignalScope::paintSpectrum (juce::Graphics& g) const
{
    const auto well = spectrumBounds.toFloat();

    g.setColour (colour::wellDeep);
    g.fillRoundedRectangle (well, radius::sm);

    // Two pixels off the bottom, not one: a floor line drawn on the well's own
    // edge disappears into it, and the spectrum then reads as broken beside a
    // scope whose baseline is plainly visible. The bars grow from this line.
    const auto inner = well.reduced (1.0f, 2.0f);

    g.setColour (colour::divider);
    g.drawHorizontalLine (juce::roundToInt (inner.getBottom()), inner.getX(), inner.getRight());

    if (idle)
        return;

    const auto bandWidth = inner.getWidth() / (float) numBands;

    // One colour for every band: the mixer's green-amber-red ramp encodes
    // headroom on a peak meter, and a band at -3dB is not "danger", it is loud.
    g.setColour (colour::accent);

    for (int band = 0; band < numBands; ++band)
    {
        const auto level = bandLevels[(size_t) band];

        if (level <= 0.0f)
            continue;

        const auto db = juce::Decibels::gainToDecibels (level, (float) spectrumFloorDb);
        const auto proportion = juce::jlimit (0.0f, 1.0f,
                                              (float) ((db - spectrumFloorDb) / -spectrumFloorDb));

        if (proportion <= 0.0f)
            continue;

        const auto height = juce::jmax (1.0f, proportion * inner.getHeight());

        g.fillRect (juce::Rectangle<float> (inner.getX() + (float) band * bandWidth,
                                            inner.getBottom() - height,
                                            juce::jmax (1.0f, bandWidth - 1.0f),
                                            height));
    }
}

void SignalScope::paint (juce::Graphics& g)
{
    paintScope (g);
    paintSpectrum (g);
}

} // namespace dew
