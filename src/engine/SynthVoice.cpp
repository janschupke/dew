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

    for (auto& osc : oscillators)
        osc = {};

    numOscillators = 0;
    vibratoPhase = 0.0;
    modulated = false;
    samplesSinceStart = 0;
    samplesUntilRelease = 0;
    adsr.reset();
}

void SynthVoice::start (int pitch, float velocity, const OscBankSnapshot& bank,
                        const AmpSettings& amp, int durationSamples)
{
    currentPitch = pitch;
    level = juce::jlimit (0.0f, 1.0f, velocity);

    numOscillators = 0;

    for (int i = 0; i < bank.numSlots; ++i)
    {
        const auto& settings = bank.slots[(size_t) i];

        if (! settings.enabled)
            continue;

        auto& osc = oscillators[(size_t) numOscillators++];

        osc.wave = settings.wave;
        osc.gain = settings.gain;

        // Every oscillator starts at zero phase, as the single one did. Two
        // slots set the same way therefore sum coherently, which is what makes
        // detuning one of them audible as a beat rather than as noise.
        osc.phase = 0.0;
        osc.triangleState = 0.0;

        const auto effectivePitch = juce::jlimit (0.0, 127.0,
                                                  (double) pitch
                                                      + 12.0 * (double) settings.octave);
        const auto frequency = midiToHz (effectivePitch, (double) settings.detuneCents);

        osc.baseIncrement = frequency / currentSampleRate;
        osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement);
    }

    // A new note starts unbent; the next block re-applies whatever the wheel
    // is actually holding.
    vibratoPhase = 0.0;
    modulated = false;

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

void SynthVoice::setPitchModulation (float bendSemitones, float modulation, int numSamples) noexcept
{
    if (! active)
        return;

    const auto silent = juce::exactlyEqual (bendSemitones, 0.0f)
                     && juce::exactlyEqual (modulation, 0.0f);

    if (silent)
    {
        // Restore exactly what note-on latched, once, and then stay out of the
        // way. Assigning the same doubles back is what makes an untouched
        // controller bit-identical to no controller at all.
        if (! modulated)
            return;

        for (int i = 0; i < numOscillators; ++i)
        {
            auto& osc = oscillators[(size_t) i];
            osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement);
        }

        vibratoPhase = 0.0;
        modulated = false;
        return;
    }

    const auto depth = juce::jlimit (0.0f, 1.0f, modulation) * maxVibratoSemitones;

    // Advance first, so a block's vibrato is the value at its start and the
    // LFO keeps moving at the same rate whatever the block size is.
    vibratoPhase += (double) vibratoHz * (double) juce::jmax (0, numSamples) / currentSampleRate;
    vibratoPhase -= std::floor (vibratoPhase);

    const auto vibrato = (double) depth
                       * std::sin (juce::MathConstants<double>::twoPi * vibratoPhase);

    const auto semitones = (double) bendSemitones + vibrato;
    const auto factor = std::pow (2.0, semitones / 12.0);

    for (int i = 0; i < numOscillators; ++i)
    {
        auto& osc = oscillators[(size_t) i];
        osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement * factor);
    }

    modulated = true;
}

float SynthVoice::Oscillator::nextSample() noexcept
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

        // Summed plainly, each by its own gain. Dividing by the number of
        // enabled oscillators would make switching one on quieten the ones
        // already playing, which is not what a second oscillator is for.
        float sum = 0.0f;

        for (int o = 0; o < numOscillators; ++o)
            sum += oscillators[(size_t) o].nextSample() * oscillators[(size_t) o].gain;

        buffer[i] += sum * envelope * level;

        ++samplesSinceStart;

        if (! adsr.isActive())
        {
            active = false;
            break;
        }
    }
}

} // namespace dew
