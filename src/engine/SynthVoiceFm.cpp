// =============================================================================
// The voice's other render loop: the same three passes with the FM matrix
// threaded through them.
//
// Its own translation unit rather than a branch inside renderAdd, for two
// reasons. The plain loop has to stay the arithmetic it is - float addition is
// not associative and four test files pin this engine sample for sample - and
// SynthVoice.cpp is already close to the length gate.
//
// What the two share is the note's own lifecycle, which is beginSample and
// endSample in the header rather than a second copy here: the release
// countdown, the envelope and the end of a note are exactly the things two
// copies would learn to disagree about.
// =============================================================================

#include "engine/SynthVoice.h"

#include <cmath>

#include "engine/MixerBus.h"

namespace dew
{

void SynthVoice::renderAddFm (float* mono, int numSamples, float* panLeft, float* panRight) noexcept
{
    // Hoisted, not declared per sample: the render path allocates nothing and
    // a fixed-size array on the stack is free, but writing it once says that
    // the offsets are recomputed rather than carried between samples.
    std::array<double, kMaxOscillators> offset {};

    for (int i = 0; i < numSamples; ++i)
    {
        float envelope = 0.0f;

        if (! beginSample (envelope))
            continue;

        // Every destination's offset, from what each source put out LAST
        // sample. Computed for all of them before any oscillator advances,
        // which is what makes the answer independent of the order the passes
        // below happen to run in - and what lets a slot modulate itself.
        for (int dst = 0; dst < kMaxOscillators; ++dst)
        {
            auto sumMod = 0.0;

            for (int src = 0; src < kMaxOscillators; ++src)
                sumMod += (double) fmAmount[(size_t) src][(size_t) dst]
                          * (double) slotOut[(size_t) src];

            offset[(size_t) dst] = maxFmPhaseOffset * sumMod;
        }

        float sum = 0.0f;

        // The same three passes renderAdd runs, in the same order, each
        // oscillator reading its own SLOT's offset rather than its position in
        // the run - note-on partitioned these arrays and the matrix did not.
        for (int o = 0; o < numMonoOscillators; ++o)
        {
            auto& osc = oscillators[(size_t) o];
            const auto raw = osc.nextSample (offset[(size_t) osc.slot]) * osc.gain;

            slotOut[(size_t) osc.slot] = raw * envelope;
            sum += raw * fmOut[(size_t) osc.slot];
        }

        for (int w = 0; w < numMonoWavetables; ++w)
        {
            auto& osc = wavetables[(size_t) w];

            // The wavetable form applies its own gain, because it carries the
            // unison normalisation. Mirrored from renderAdd rather than
            // unified, for the same reason it is separate there.
            const auto raw = osc.nextSample (envelope, offset[(size_t) osc.slot]);

            slotOut[(size_t) osc.slot] = raw * envelope;
            sum += raw * fmOut[(size_t) osc.slot];
        }

        mono[i] += sum * envelope * level;

        for (int k = 0; k < numLfos; ++k)
        {
            auto& lfo = lfos[(size_t) k];

            const auto moved = lfo.value();
            lfo.advance();

            const auto isWavetable = lfo.wavetableIndex >= 0;

            const auto slot = isWavetable ? wavetables[(size_t) lfo.wavetableIndex].slot
                                          : oscillators[(size_t) lfo.classicIndex].slot;

            const auto raw = isWavetable ? wavetables[(size_t) lfo.wavetableIndex].nextSample (
                                               envelope, offset[(size_t) slot])
                                         : oscillators[(size_t) lfo.classicIndex].nextSample (
                                               offset[(size_t) slot])
                                               * oscillators[(size_t) lfo.classicIndex].gain;

            // What this slot SENDS carries the envelope and its own gain, and
            // deliberately neither the volume swing below nor the note's
            // velocity. Both of those say how loud the result is; folding them
            // into the modulator would make a knob marked VOL change the
            // timbre of everything this oscillator is routed into.
            slotOut[(size_t) slot] = raw * envelope;

            // Attenuation only, never boost, and exactly unity at depth zero -
            // see renderAdd, which states the arithmetic and why.
            const auto swing = 1.0f - (std::abs (lfo.toVolume) - lfo.toVolume * moved) * 0.5f;
            const auto value = raw * fmOut[(size_t) slot] * swing * envelope * level;

            if (panLeft == nullptr)
            {
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
