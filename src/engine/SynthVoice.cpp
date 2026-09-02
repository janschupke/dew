#include "SynthVoice.h"

#include <cmath>

namespace dew
{

namespace
{

/** PolyBLEP correction at a discontinuity: `t` is the phase in [0, 1) and `dt`
    the per-sample phase increment. Subtracting this from a naive saw, or adding
    it at each edge of a square, removes most of the aliasing the sharp step
    would otherwise fold back into the audible range.
*/
inline double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0;

    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }

    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }

    return 0.0;
}

inline double midiToHz (double pitch, double detuneCents) noexcept
{
    return 440.0 * std::pow (2.0, (pitch - 69.0 + detuneCents / 100.0) / 12.0);
}

} // namespace

void SynthVoice::prepare (double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    adsr.setSampleRate (currentSampleRate);
    reset();
}

void SynthVoice::reset() noexcept
{
    active = false;
    phase = 0.0;
    phaseIncrement = 0.0;
    triangleState = 0.0;
    samplesSinceStart = 0;
    samplesUntilRelease = 0;
    adsr.reset();
}

void SynthVoice::start (int pitch, float velocity, const OscSettings& osc,
                        const AmpSettings& amp, int durationSamples)
{
    currentPitch = pitch;
    wave = osc.wave;
    oscGain = osc.gain;
    level = juce::jlimit (0.0f, 1.0f, velocity);

    const auto effectivePitch = juce::jlimit (0.0, 127.0,
                                              (double) pitch + 12.0 * (double) osc.octave);
    const auto frequency = midiToHz (effectivePitch, (double) osc.detuneCents);

    phase = 0.0;
    triangleState = 0.0;
    phaseIncrement = juce::jlimit (0.0, 0.5, frequency / currentSampleRate);

    adsrParams.attack  = amp.attack;
    adsrParams.decay   = amp.decay;
    adsrParams.sustain = amp.sustain;
    adsrParams.release = amp.release;
    adsr.setParameters (adsrParams);

    adsr.reset();
    adsr.noteOn();

    samplesSinceStart = 0;
    samplesUntilRelease = juce::jmax ((juce::int64) 1, (juce::int64) durationSamples);
    active = true;
}

void SynthVoice::release() noexcept
{
    if (active)
    {
        adsr.noteOff();
        samplesUntilRelease = 0;
    }
}

float SynthVoice::nextSample() noexcept
{
    const auto t = phase;
    const auto dt = phaseIncrement;

    double value = 0.0;

    switch (wave)
    {
        case Waveform::sine:
            value = std::sin (juce::MathConstants<double>::twoPi * t);
            break;

        case Waveform::saw:
            value = 2.0 * t - 1.0 - polyBlep (t, dt);
            break;

        case Waveform::square:
            value = t < 0.5 ? 1.0 : -1.0;
            value += polyBlep (t, dt);
            value -= polyBlep (std::fmod (t + 0.5, 1.0), dt);
            break;

        case Waveform::triangle:
        {
            // Integrate the band-limited square. The leak term stops DC from
            // accumulating over a long held note.
            double square = t < 0.5 ? 1.0 : -1.0;
            square += polyBlep (t, dt);
            square -= polyBlep (std::fmod (t + 0.5, 1.0), dt);

            triangleState = 4.0 * dt * square + (1.0 - 4.0 * dt) * triangleState;
            value = triangleState;
            break;
        }
    }

    phase += dt;

    if (phase >= 1.0)
        phase -= 1.0;

    return (float) value;
}

void SynthVoice::renderAdd (float* buffer, int numSamples) noexcept
{
    if (! active)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesUntilRelease > 0 && --samplesUntilRelease == 0)
            adsr.noteOff();

        const auto envelope = adsr.getNextSample();

        buffer[i] += nextSample() * envelope * level * oscGain;

        ++samplesSinceStart;

        if (! adsr.isActive())
        {
            active = false;
            break;
        }
    }
}

} // namespace dew
