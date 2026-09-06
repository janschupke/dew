// =============================================================================
// SynthVoice - the settings that reach a note ALREADY SOUNDING.
//
// One of three translation units behind engine/SynthVoice.h. Here because
// SynthVoice.cpp had reached the four hundred code lines the tree allows a
// file, and because these three are one idea rather than three: everything else
// about a voice is latched at note-on, and each of these is an argued exception
// to that. The arguments are in the header, above each declaration.
//
// What they have in common is the shape. Each walks the voice's own compacted
// state, finds the SLOT each entry came from, and assigns that slot's current
// value back - so a project nothing is automating writes every field its own
// value and renders the bits it always rendered.
// =============================================================================

#include "engine/SynthVoice.h"

namespace dew
{

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

void SynthVoice::setLfo (const OscBankSnapshot& bank) noexcept
{
    if (! active || numLfos == 0)
        return;

    for (int l = 0; l < numLfos; ++l)
    {
        auto& lfo = lfos[(size_t) l];

        // Through the oscillator rather than from a slot stored twice: the
        // voice compacted its enabled slots at note-on, and the oscillator it
        // landed on is the one that already remembers which slot it came from.
        const auto slot = lfo.classicIndex >= 0 ? oscillators[(size_t) lfo.classicIndex].slot
                                                : wavetables[(size_t) lfo.wavetableIndex].slot;

        if (slot < 0 || slot >= bank.numSlots)
            continue;

        const auto& settings = bank.slots[(size_t) slot];

        // Everything except the phase - see the header. The rate arrives
        // already resolved, free or synced, exactly as startLfo receives it.
        lfo.increment = (double) settings.lfoHz / currentSampleRate;
        lfo.wave = settings.lfoWave;
        lfo.toPitch = settings.lfoToPitch;
        lfo.toVolume = settings.lfoToVolume;
        lfo.toPan = settings.lfoToPan;

        panned = panned || ! juce::exactlyEqual (settings.lfoToPan, 0.0f);
    }
}

} // namespace dew
