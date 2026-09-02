#pragma once

#include <array>
#include <vector>

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "design/Tokens.h"
#include "model/Constants.h"

namespace dew
{

class AudioEngine;

/** The master output as a waveform and as a spectrum, side by side.

    Two wells rather than two components. They show the same samples analysed
    two ways, so one timer, one window and one transform means a transient can
    never appear in one of them a frame before the other.

    Everything the widget shows is a pure function of the last pushFrame(), and
    the timer does nothing except take a window off the engine's SignalTap and
    call it. That is the whole reason the seam is there: the design gallery, the
    screenshot tool and every test drive this with a signal they generated, with
    no engine, no audio device and no clock.
*/
class SignalScope : public juce::Component,
                    private juce::Timer
{
public:
    /** The engine is optional, exactly as the mixer's is. */
    explicit SignalScope (AudioEngine* = nullptr);
    ~SignalScope() override;

    static constexpr int wellWidth = 132;
    static constexpr int preferredWidth = wellWidth * 2 + tokens::space::sm;

    static constexpr int windowSamples = 2048;
    static constexpr int fftOrder = 11;
    static_assert (1 << fftOrder == windowSamples, "the window is the transform size");

    /** Twenty-four bands from 30Hz to 18kHz is close to third-octave, which is
        the resolution an analyser is conventionally read at. At 132 pixels it
        is a 4px bar with a gap; more bands become a texture and fewer make each
        band a region rather than a frequency.
    */
    static constexpr int numBands = 24;
    static constexpr double bandLowHz = 30.0;
    static constexpr double bandHighHz = 18000.0;

    /** Span in milliseconds, not samples, so a 100Hz tone shows the same two
        cycles whatever device is open.
    */
    static constexpr double displaySpanMs = 20.0;

    /** Deliberately not the mixer's -48. That floor is for a peak meter reading
        a whole bus; one band's share of a normal mix sits well below it, and at
        -48 most of this display would rest on the floor.
    */
    static constexpr double spectrumFloorDb = -72.0;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The only way anything gets in. `numSamples` may be short of
        windowSamples; the window fills from the right, because a short frame is
        missing history, not missing present.
    */
    void pushFrame (const float* mono, int numSamples, double sampleRate);

    // --- read-only, for the tests and the gallery ----------------------------
    bool isIdle() const noexcept       { return idle; }
    bool isTriggered() const noexcept  { return triggered; }
    int getTriggerOffset() const noexcept { return triggerOffset; }
    int getDisplaySamples() const noexcept { return displaySamples; }

    int getNumBands() const noexcept { return numBands; }

    /** Low and high as well as centre, so a test can assert "the band that
        CONTAINS this frequency is the loudest" without reconstructing the band
        spacing - which would make the test a copy of the implementation.
    */
    double getBandLowHz (int band) const noexcept;
    double getBandCentreHz (int band) const noexcept;
    double getBandHighHz (int band) const noexcept;

    float getBandLevel (int band) const noexcept;  ///< 0..1, after ballistics
    float getBandDb (int band) const noexcept;

    /** So a test can assert the wells do not move when sound starts. */
    juce::Rectangle<int> getScopeBounds() const noexcept    { return scopeBounds; }
    juce::Rectangle<int> getSpectrumBounds() const noexcept { return spectrumBounds; }

private:
    void timerCallback() override;

    void analyseSpectrum (double sampleRate);
    void rebuildBandBins (double sampleRate);
    int findTrigger (int displayStart, int searchSamples, float peak) const;
    void rebuildColumns();

    void paintScope (juce::Graphics&) const;
    void paintSpectrum (juce::Graphics&) const;

    /** Below this the display is empty rather than showing the noise floor. */
    static constexpr float traceFloor = 1.0e-4f;

    AudioEngine* engine = nullptr;

    std::array<float, windowSamples> window {};
    std::array<float, windowSamples * 2> fftData {};

    juce::dsp::FFT fft { fftOrder };

    /** Hann rather than rectangular: the window is an arbitrary slice of a
        continuous signal, and a rectangular window's sidelobes would smear one
        sine across every band.

        Explicitly UNnormalised. JUCE normalises by default, which divides the
        window by its own coherent gain - so the gain is compensated twice and a
        full-scale sine reads 6dB hot. Turning it off here keeps the whole
        calibration in one visible constant in analyseSpectrum().
    */
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) windowSamples,
                                                    juce::dsp::WindowingFunction<float>::hann,
                                                    false };

    std::array<float, numBands> bandLevels {};
    std::array<int, numBands> bandFirstBin {}, bandLastBin {};
    double binsBuiltFor = 0.0;

    /** The trace, already in pixels: the one-pixel minimum that keeps a quiet
        signal visible needs the well's height, which the rebuild has and paint
        would have to rediscover.
    */
    std::vector<float> columnTop, columnBottom;

    float signalLevel = 0.0f;
    bool idle = true;
    bool triggered = false;
    int triggerOffset = 0;
    int displaySamples = 0;
    double frameSampleRate = kDefaultSampleRate;
    juce::int64 lastWriteCount = -1;

    juce::Rectangle<int> scopeBounds, spectrumBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalScope)
};

} // namespace dew
