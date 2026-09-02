#include "engine/SynthVoice.h"

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
    currentSampleRate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
    adsr.setSampleRate (currentSampleRate);
    reset();
}

void SynthVoice::reset() noexcept
{
    active = false;

    for (auto& osc : oscillators)
        osc = {};

    for (auto& osc : wavetables)
        osc = {};

    numOscillators = 0;
    numWavetables = 0;
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
    numWavetables = 0;

    for (int i = 0; i < bank.numSlots; ++i)
    {
        const auto& settings = bank.slots[(size_t) i];

        if (! settings.enabled)
            continue;

        const auto effectivePitch = juce::jlimit (0.0, 127.0,
                                                  (double) pitch
                                                      + 12.0 * (double) settings.octave);

        if (settings.mode == OscMode::wavetable)
        {
            auto& osc = wavetables[(size_t) numWavetables++];

            osc.table = &wavetableAt (settings.table);
            osc.slot = i;
            osc.numUnison = juce::jlimit (1, kMaxUnisonVoices, settings.unisonVoices);

            osc.basePosition = settings.position;
            osc.positionMod = settings.positionMod;
            osc.source = settings.positionSource;
            osc.lfoPhase = 0.0;
            osc.lfoIncrement = (double) settings.positionRate / currentSampleRate;

            // Normalised WITHIN the slot, which is the opposite of the rule
            // across slots. Adding an oscillator is asking for a second sound
            // and should be louder; stacking unison is asking for the SAME
            // sound to be thicker, and one that got seven times louder as you
            // turned it up would be unusable. sqrt rather than 1/n because the
            // copies are detuned, so they sum closer to incoherently than not.
            osc.gain = settings.gain / std::sqrt ((float) osc.numUnison);

            for (int u = 0; u < osc.numUnison; ++u)
            {
                const auto spread = osc.numUnison > 1
                                  ? (double) settings.unisonDetune
                                        * (2.0 * (double) u / (double) (osc.numUnison - 1) - 1.0)
                                  : 0.0;

                const auto frequency = midiToHz (effectivePitch,
                                                 (double) settings.detuneCents + spread);

                osc.baseIncrement[(size_t) u] = frequency / currentSampleRate;
                osc.phaseIncrement[(size_t) u] = juce::jlimit (0.0, 0.5,
                                                               osc.baseIncrement[(size_t) u]);

                // Spread across the cycle, not all at zero. Seven copies
                // starting together sum into a click and begin their detune in
                // unison; spreading them is deterministic, which randomising
                // would not be - and a render here has to be reproducible.
                osc.phase[(size_t) u] = osc.numUnison > 1
                                      ? (double) u / (double) osc.numUnison
                                      : 0.0;
            }

            osc.updateMip();
            continue;
        }

        auto& osc = oscillators[(size_t) numOscillators++];

        osc.wave = settings.wave;
        osc.gain = settings.gain;

        // Every oscillator starts at zero phase, as the single one did. Two
        // slots set the same way therefore sum coherently, which is what makes
        // detuning one of them audible as a beat rather than as noise.
        osc.phase = 0.0;
        osc.triangleState = 0.0;

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

        for (int w = 0; w < numWavetables; ++w)
        {
            auto& osc = wavetables[(size_t) w];

            for (int u = 0; u < osc.numUnison; ++u)
                osc.phaseIncrement[(size_t) u] = juce::jlimit (0.0, 0.5,
                                                               osc.baseIncrement[(size_t) u]);

            osc.updateMip();
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

    for (int w = 0; w < numWavetables; ++w)
    {
        auto& osc = wavetables[(size_t) w];

        for (int u = 0; u < osc.numUnison; ++u)
            osc.phaseIncrement[(size_t) u] = juce::jlimit (0.0, 0.5,
                                                           osc.baseIncrement[(size_t) u] * factor);

        // A bend moves the increments, and the increments are what decide which
        // band-limited copy is safe to read. Per block, like the bend itself.
        osc.updateMip();
    }

    modulated = true;
}

void SynthVoice::setWavetablePosition (const OscBankSnapshot& bank) noexcept
{
    if (! active || numWavetables == 0)
        return;

    for (int w = 0; w < numWavetables; ++w)
    {
        auto& osc = wavetables[(size_t) w];

        if (osc.slot >= 0 && osc.slot < bank.numSlots)
            osc.basePosition = juce::jlimit (0.0f, 1.0f,
                                             bank.slots[(size_t) osc.slot].position);
    }
}

void SynthVoice::WavetableOscillator::updateMip() noexcept
{
    // The most demanding unison copy decides for all of them: the widest
    // detune is the one closest to folding, and reading one table per slot is
    // what makes unison affordable in the first place.
    double highest = 0.0;

    for (int u = 0; u < numUnison; ++u)
        highest = juce::jmax (highest, phaseIncrement[(size_t) u]);

    mip = wavetableMipFor (highest);
}

float SynthVoice::WavetableOscillator::nextSample (float envelope) noexcept
{
    if (table == nullptr)
        return 0.0f;

    auto modulator = envelope;

    if (source == PositionSource::lfo)
    {
        modulator = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);

        lfoPhase += lfoIncrement;

        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
    }

    // Per sample, not per block. The position is what the ear is listening to
    // on a wavetable, and a value that only stepped at block boundaries would
    // be audible as a zipper on anything but the slowest sweep.
    const auto position = juce::jlimit (0.0f, 1.0f, basePosition + positionMod * modulator);

    float sum = 0.0f;

    for (int u = 0; u < numUnison; ++u)
    {
        sum += table->at (position, mip, phase[(size_t) u]);

        phase[(size_t) u] += phaseIncrement[(size_t) u];

        if (phase[(size_t) u] >= 1.0)
            phase[(size_t) u] -= 1.0;
    }

    // Gain applied here rather than by the caller, because it carries the
    // unison normalisation and the caller has no business knowing about that.
    return sum * gain;
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

        // A second, separately counted pass rather than a branch inside the
        // first. On a voice whose slots are all classic numWavetables is zero,
        // so the sum above is the same float in the same order it always was -
        // which is what the pinned renders are pinned to.
        for (int w = 0; w < numWavetables; ++w)
            sum += wavetables[(size_t) w].nextSample (envelope);

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
