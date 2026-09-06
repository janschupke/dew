#include "engine/SynthVoice.h"

#include <cmath>

#include "engine/MixerBus.h"

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

    for (auto& lfo : lfos)
        lfo = {};

    numMonoOscillators = 0;
    numLfoOscillators = 0;
    numMonoWavetables = 0;
    numLfoWavetables = 0;
    numLfos = 0;
    panned = false;

    fmAmount = {};
    fmOut = {};
    slotOut = {};
    fmActive = false;

    vibratoPhase = 0.0;
    bendFactor = 1.0;
    modulated = false;
    samplesSinceStart = 0;
    samplesUntilRelease = 0;
    samplesUntilStart = 0;
    adsr.reset();
}

void SynthVoice::startLfo (const OscSettings& settings, int classicIndex,
                           int wavetableIndex) noexcept
{
    auto& lfo = lfos[(size_t) numLfos++];

    // Phase zero, every note. Retriggering is what makes a rendered note sound
    // the same wherever it falls in the arrangement, and it is what the one
    // other LFO in this file already does with its wavetable position.
    lfo.phase = 0.0;

    // The rate arrives already resolved - free or synced, the reader worked it
    // out on the message thread. See OscSettings::lfoHz.
    lfo.increment = (double) settings.lfoHz / currentSampleRate;

    lfo.wave = settings.lfoWave;
    lfo.toPitch = settings.lfoToPitch;
    lfo.toVolume = settings.lfoToVolume;
    lfo.toPan = settings.lfoToPan;
    lfo.classicIndex = classicIndex;
    lfo.wavetableIndex = wavetableIndex;

    panned = panned || ! juce::exactlyEqual (settings.lfoToPan, 0.0f);
}

void SynthVoice::start (int pitch, float velocity, const OscBankSnapshot& bank,
                        const AmpSettings& amp, int durationSamples, int startOffset)
{
    currentPitch = pitch;
    level = juce::jlimit (0.0f, 1.0f, velocity);

    numMonoOscillators = 0;
    numLfoOscillators = 0;
    numMonoWavetables = 0;
    numLfoWavetables = 0;
    numLfos = 0;
    panned = false;

    // The matrix, latched whole. Every slot's row is copied whether or not the
    // slot is enabled: a disabled slot renders nothing, so its row multiplies a
    // `slotOut` that stays at zero and its column is never read - which is
    // cheaper than deciding that here and re-deciding it every sample.
    slotOut = {};
    fmActive = bank.anyFm;

    for (int src = 0; src < kMaxOscillators; ++src)
    {
        const auto& row = bank.slots[(size_t) src];

        for (int dst = 0; dst < kMaxOscillators; ++dst)
            fmAmount[(size_t) src][(size_t) dst] = juce::jlimit (0.0f, 1.0f,
                                                                 row.fmTo[(size_t) dst]);

        fmOut[(size_t) src] = juce::jlimit (0.0f, 1.0f, row.fmOut);
    }

    // Two passes over the bank, the slots with no LFO first, so each array ends
    // up partitioned: the plain pass, then the LFO pass.
    //
    // The order within the first pass is therefore exactly the order the single
    // pass produced, on any voice where no slot has an LFO asking for anything -
    // and float addition is not associative, so an oscillator that merely moved
    // within the run would change the last bits of every note this engine has
    // ever rendered. That is what the whole partition is for.
    for (const auto wantLfo : { false, true })
    {
        for (int i = 0; i < bank.numSlots; ++i)
        {
            const auto& settings = bank.slots[(size_t) i];

            if (! settings.enabled || settings.lfoActive != wantLfo)
                continue;

            const auto effectivePitch = juce::jlimit (
                0.0, 127.0, (double) pitch + 12.0 * (double) settings.octave);

            if (settings.mode == OscMode::wavetable)
            {
                const auto index = wantLfo ? numMonoWavetables + numLfoWavetables++
                                           : numMonoWavetables++;

                auto& osc = wavetables[(size_t) index];

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
                                                  * (2.0 * (double) u / (double) (osc.numUnison - 1)
                                                     - 1.0)
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
                    osc.phase[(size_t) u] = osc.numUnison > 1 ? (double) u / (double) osc.numUnison
                                                              : 0.0;
                }

                osc.updateMip();

                if (wantLfo)
                    startLfo (settings, /*classicIndex*/ -1, index);

                continue;
            }

            const auto index = wantLfo ? numMonoOscillators + numLfoOscillators++
                                       : numMonoOscillators++;

            auto& osc = oscillators[(size_t) index];

            osc.wave = settings.wave;
            osc.gain = settings.gain;
            osc.slot = i;

            // Every oscillator starts at zero phase, as the single one did. Two
            // slots set the same way therefore sum coherently, which is what makes
            // detuning one of them audible as a beat rather than as noise.
            osc.phase = 0.0;
            osc.triangleState = 0.0;

            const auto frequency = midiToHz (effectivePitch, (double) settings.detuneCents);

            osc.baseIncrement = frequency / currentSampleRate;
            osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement);

            if (wantLfo)
                startLfo (settings, index, /*wavetableIndex*/ -1);
        }
    }

    // A new note starts unbent; the next block re-applies whatever the wheel
    // is actually holding.
    vibratoPhase = 0.0;
    modulated = false;

    adsrParams.attack = amp.attack;
    adsrParams.decay = amp.decay;
    adsrParams.sustain = amp.sustain;
    adsrParams.release = amp.release;
    adsr.setParameters (adsrParams);

    adsr.reset();
    adsr.noteOn();

    samplesSinceStart = 0;
    samplesUntilRelease = juce::jmax ((juce::int64) 1, (juce::int64) durationSamples);
    samplesUntilStart = juce::jmax ((juce::int64) 0, (juce::int64) startOffset);
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
        // The bend is nothing, whatever an LFO may be doing on top of it.
        // applyLfoPitch composes with this, so it has to be right even on the
        // path that returns early.
        bendFactor = 1.0;

        if (! modulated)
            return;

        // EVERY oscillator, both partitions. Stopping at the plain run would
        // leave a bend or the mod wheel silently unable to reach any slot whose
        // LFO happened to be on.
        for (int i = 0; i < totalOscillators(); ++i)
        {
            auto& osc = oscillators[(size_t) i];
            osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement);
        }

        for (int w = 0; w < totalWavetables(); ++w)
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

    bendFactor = factor;

    for (int i = 0; i < totalOscillators(); ++i)
    {
        auto& osc = oscillators[(size_t) i];
        osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement * factor);
    }

    for (int w = 0; w < totalWavetables(); ++w)
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
    if (! active || totalWavetables() == 0)
        return;

    for (int w = 0; w < totalWavetables(); ++w)
    {
        auto& osc = wavetables[(size_t) w];

        if (osc.slot >= 0 && osc.slot < bank.numSlots)
            osc.basePosition = juce::jlimit (0.0f, 1.0f, bank.slots[(size_t) osc.slot].position);
    }
}

void SynthVoice::setFmMatrix (const OscBankSnapshot& bank) noexcept
{
    // Guarded on fmActive, not just on active: a voice that latched an empty
    // matrix is running the plain path, which has no phase offsets to apply and
    // no output column to honour. Converting it half way through a note would
    // be a click. A matrix switched on reaches the next note - see the header.
    if (! active || ! fmActive)
        return;

    for (int src = 0; src < bank.numSlots; ++src)
    {
        const auto& row = bank.slots[(size_t) src];

        for (int dst = 0; dst < kMaxOscillators; ++dst)
            fmAmount[(size_t) src][(size_t) dst] = juce::jlimit (0.0f, 1.0f,
                                                                 row.fmTo[(size_t) dst]);

        fmOut[(size_t) src] = juce::jlimit (0.0f, 1.0f, row.fmOut);
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

float SynthVoice::WavetableOscillator::nextSample (float envelope, double phaseOffset) noexcept
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

    // The offset moves what every unison copy READS, and none of what they
    // accumulate - the stack keeps its own spread and its own detune. Tested
    // for rather than added, so a voice with no offset does the arithmetic it
    // has always done rather than arithmetic that happens to come out the same.
    const auto offset = juce::exactlyEqual (phaseOffset, 0.0);

    for (int u = 0; u < numUnison; ++u)
    {
        auto t = phase[(size_t) u];

        if (! offset)
        {
            t += phaseOffset;
            t -= std::floor (t);
        }

        sum += table->at (position, mip, t);

        phase[(size_t) u] += phaseIncrement[(size_t) u];

        if (phase[(size_t) u] >= 1.0)
            phase[(size_t) u] -= 1.0;
    }

    // Gain applied here rather than by the caller, because it carries the
    // unison normalisation and the caller has no business knowing about that.
    return sum * gain;
}

float SynthVoice::Oscillator::nextSample (double phaseOffset) noexcept
{
    auto t = phase;
    const auto dt = phaseIncrement;

    // Phase MODULATION, not frequency modulation: the offset moves the point
    // this sample is read from and leaves the accumulator alone, so an index
    // that returns to zero returns the oscillator to the pitch it was playing
    // rather than to wherever integrating an offset had carried it.
    //
    // The band limiting is computed for the UNMODULATED increment, so a
    // modulated saw or square aliases. That is the honest trade and not a
    // defect to fix: correcting it means a new PolyBLEP per sample against a
    // discontinuity whose position the modulator has just moved. Sine is the
    // waveform FM is for, and the other three are still there.
    if (! juce::exactlyEqual (phaseOffset, 0.0))
    {
        t += phaseOffset;
        t -= std::floor (t);
    }

    double value = 0.0;

    switch (wave)
    {
        case Waveform::sine: value = std::sin (juce::MathConstants<double>::twoPi * t); break;

        case Waveform::saw: value = 2.0 * t - 1.0 - polyBlep (t, dt); break;

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

void SynthVoice::applyLfoPitch() noexcept
{
    for (int k = 0; k < numLfos; ++k)
    {
        auto& lfo = lfos[(size_t) k];

        if (juce::exactlyEqual (lfo.toPitch, 0.0f))
            continue;

        // The value the previous block left, which IS this block's starting
        // value - setPitchModulation and this both run before the sample loop
        // advances the phase. One accumulator, so there is nothing to drift.
        const auto factor = std::pow (2.0, (double) (lfo.toPitch * lfo.value()) / 12.0);

        if (lfo.classicIndex >= 0)
        {
            auto& osc = oscillators[(size_t) lfo.classicIndex];
            osc.phaseIncrement = juce::jlimit (0.0, 0.5, osc.baseIncrement * bendFactor * factor);
            continue;
        }

        auto& osc = wavetables[(size_t) lfo.wavetableIndex];

        for (int u = 0; u < osc.numUnison; ++u)
            osc.phaseIncrement[(size_t) u] = juce::jlimit (
                0.0, 0.5, osc.baseIncrement[(size_t) u] * bendFactor * factor);

        // The increments decide which band-limited copy is safe to read, and
        // they have just moved. Per block, like the bend's own.
        osc.updateMip();
    }
}

void SynthVoice::renderAdd (float* mono, int numSamples, float* panLeft, float* panRight) noexcept
{
    if (! active)
        return;

    applyLfoPitch();

    // The matrix is a whole second loop, in SynthVoiceFm.cpp. Everything below
    // has to stay the expressions it is, in the order it is - float addition is
    // not associative and four test files pin this engine sample for sample -
    // so the two paths are separated here, once per block, rather than by a
    // branch somewhere inside them.
    if (fmActive)
    {
        renderAddFm (mono, numSamples, panLeft, panRight);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float envelope = 0.0f;

        // Not yet. Contributing nothing rather than silence through the
        // envelope: the note has not begun, so neither has its attack.
        if (! beginSample (envelope))
            continue;

        // Summed plainly, each by its own gain. Dividing by the number of
        // enabled oscillators would make switching one on quieten the ones
        // already playing, which is not what a second oscillator is for.
        float sum = 0.0f;

        for (int o = 0; o < numMonoOscillators; ++o)
            sum += oscillators[(size_t) o].nextSample() * oscillators[(size_t) o].gain;

        // A second, separately counted pass rather than a branch inside the
        // first. On a voice whose slots are all classic numMonoWavetables is 0,
        // so the sum above is the same float in the same order it always was -
        // which is what the pinned renders are pinned to.
        for (int w = 0; w < numMonoWavetables; ++w)
            sum += wavetables[(size_t) w].nextSample (envelope);

        mono[i] += sum * envelope * level;

        // A THIRD, separately counted pass, for the reason the second one is
        // separate: on a voice whose slots have no LFO numLfos is zero, so
        // everything above is the same arithmetic in the same order it always
        // was - which is what the pinned renders are pinned to.
        for (int k = 0; k < numLfos; ++k)
        {
            auto& lfo = lfos[(size_t) k];

            const auto moved = lfo.value();
            lfo.advance();

            // The wavetable form applies its own gain, because it carries the
            // unison normalisation; the classic one does not. Mirrored from the
            // two passes above rather than unified, for the same reason.
            const auto raw = lfo.wavetableIndex >= 0
                                 ? wavetables[(size_t) lfo.wavetableIndex].nextSample (envelope)
                                 : oscillators[(size_t) lfo.classicIndex].nextSample()
                                       * oscillators[(size_t) lfo.classicIndex].gain;

            // Attenuation only, never boost, and exactly unity at depth zero:
            //
            //     d = +1  ->  (1 + moved) / 2      d = 0  ->  1
            //     d = -1  ->  (1 - moved) / 2
            //
            // A bipolar depth against a unity centre would otherwise double the
            // oscillator at its peak, which is six decibels nobody asked for by
            // turning a knob marked VOL.
            const auto swing = 1.0f - (std::abs (lfo.toVolume) - lfo.toVolume * moved) * 0.5f;
            const auto value = raw * swing * envelope * level;

            if (panLeft == nullptr)
            {
                // Nowhere to put a side. The pitch and the level still move;
                // the pan has nothing to move across.
                mono[i] += value;
                continue;
            }

            float leftGain = 1.0f, rightGain = 1.0f;
            MixerBus::modulationPanGains (lfo.toPan * moved, leftGain, rightGain);

            panLeft[i] += value * leftGain;
            panRight[i] += value * rightGain;
        }

        if (! endSample())
            break;
    }
}

} // namespace dew
