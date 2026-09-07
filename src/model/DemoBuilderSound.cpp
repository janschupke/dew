// The instrument half of the demo vocabulary: what a channel sounds like.
//
// Split from DemoBuilders.cpp when the library grew to ten tracks and the FM
// matrix and the LFO joined the things a demo has to be able to say. The gate
// caps a source file at 400 code lines, and this is the seam the header already
// draws.

#include "model/DemoBuilders.h"

#include "model/GeneratorCatalog.h"
#include "model/ProjectEdits.h"

namespace dew::demo
{

namespace
{

juce::ValueTree oscSlot (juce::ValueTree channel, int slot)
{
    return ProjectEdits::oscillatorAt (channel, slot);
}

/** The LFO node of one oscillator slot.

    An LFO is per SLOT, not per channel - three oscillators carry three of them
    - which is why every function here takes a slot and why a channel with one
    vibrato still has to say which voice is doing the shaking.
*/
juce::ValueTree lfoOf (juce::ValueTree channel, int slot)
{
    return oscSlot (channel, slot).getChildWithName (ids::LFO);
}

void setLfoDepths (juce::ValueTree lfo, const juce::String& wave, double toPitch, double toVolume,
                   double toPan)
{
    lfo.setProperty (ids::lfoOn, true, nullptr);
    lfo.setProperty (ids::lfoWave, wave, nullptr);
    lfo.setProperty (ids::lfoToPitch, stored (toPitch), nullptr);
    lfo.setProperty (ids::lfoToVolume, stored (toVolume), nullptr);
    lfo.setProperty (ids::lfoToPan, stored (toPan), nullptr);
}

} // namespace

void setClassicOsc (juce::ValueTree channel, int slot, const juce::String& wave, int octave,
                    double gain, int detuneCents)
{
    auto osc = oscSlot (channel, slot);

    if (! osc.isValid())
        return;

    osc.setProperty (ids::enabled, true, nullptr);
    osc.setProperty (ids::mode, "classic", nullptr);
    generatorNodeFor (osc, ids::wave).setProperty (ids::wave, wave, nullptr);
    osc.setProperty (ids::octave, octave, nullptr);

    // A DOUBLE, though cents are whole numbers and the argument is an int. The
    // schema declares detuneCents as a double, so a file round-trips to one -
    // and a tree holding var(37) beside a file holding var(37.0) is a tree that
    // serialises to identical bytes and fails isEquivalentTo. That is the
    // inverse of the trap the byte-comparison test exists for, and only the
    // tree comparison can see it.
    osc.setProperty (ids::detuneCents, (double) detuneCents, nullptr);
    osc.setProperty (ids::gain, stored (gain), nullptr);
}

void setWavetableOsc (juce::ValueTree channel, int slot, const juce::String& table, double position,
                      double mod, const juce::String& source, double rate, int unisonVoices,
                      double unisonDetune, int octave, double gain)
{
    auto osc = oscSlot (channel, slot);

    if (! osc.isValid())
        return;

    osc.setProperty (ids::enabled, true, nullptr);
    osc.setProperty (ids::mode, "wavetable", nullptr);

    auto wavetable = generatorNodeFor (osc, ids::wavePosition);
    wavetable.setProperty (ids::wavetable, table, nullptr);
    wavetable.setProperty (ids::wavePosition, stored (position), nullptr);
    wavetable.setProperty (ids::wavePositionMod, stored (mod), nullptr);
    wavetable.setProperty (ids::wavePositionSource, source, nullptr);
    wavetable.setProperty (ids::wavePositionRate, stored (rate), nullptr);
    wavetable.setProperty (ids::unisonVoices, unisonVoices, nullptr);
    wavetable.setProperty (ids::unisonDetune, stored (unisonDetune), nullptr);

    osc.setProperty (ids::octave, octave, nullptr);
    osc.setProperty (ids::gain, stored (gain), nullptr);
}

void setFm (juce::ValueTree channel, int slot, double to1, double to2, double to3, double out)
{
    auto osc = oscSlot (channel, slot);

    if (! osc.isValid())
        return;

    // The amounts live on the SLOT rather than under its generator: a modulator
    // routes the same way whether it is a sine or a wavetable, and the matrix is
    // about the slots together rather than about what any one of them is.
    osc.setProperty (ids::fmTo1, stored (to1), nullptr);
    osc.setProperty (ids::fmTo2, stored (to2), nullptr);
    osc.setProperty (ids::fmTo3, stored (to3), nullptr);
    osc.setProperty (ids::fmOut, stored (out), nullptr);
}

void setLfo (juce::ValueTree channel, int slot, const juce::String& wave, double rateHz,
             double toPitch, double toVolume, double toPan)
{
    auto lfo = lfoOf (channel, slot);

    if (! lfo.isValid())
        return;

    setLfoDepths (lfo, wave, toPitch, toVolume, toPan);
    lfo.setProperty (ids::lfoSync, false, nullptr);
    lfo.setProperty (ids::lfoRate, stored (rateHz), nullptr);
}

void setSyncedLfo (juce::ValueTree channel, int slot, const juce::String& wave,
                   const juce::String& division, double toPitch, double toVolume, double toPan)
{
    auto lfo = lfoOf (channel, slot);

    if (! lfo.isValid())
        return;

    setLfoDepths (lfo, wave, toPitch, toVolume, toPan);
    lfo.setProperty (ids::lfoSync, true, nullptr);
    lfo.setProperty (ids::lfoDivision, division, nullptr);
}

void disableOsc (juce::ValueTree channel, int slot)
{
    if (auto osc = oscSlot (channel, slot); osc.isValid())
        osc.setProperty (ids::enabled, false, nullptr);
}

void setAmp (juce::ValueTree channel, double attack, double decay, double sustain, double release)
{
    auto amp = channel.getChildWithName (ids::INSTRUMENT).getChildWithName (ids::AMP);

    if (! amp.isValid())
        return;

    amp.setProperty (ids::attack, stored (attack), nullptr);
    amp.setProperty (ids::decay, stored (decay), nullptr);
    amp.setProperty (ids::sustain, stored (sustain), nullptr);
    amp.setProperty (ids::release, stored (release), nullptr);
}

void setMix (juce::ValueTree channel, double volume, double pan)
{
    if (! channel.isValid())
        return;

    channel.setProperty (ids::volume, stored (volume), nullptr);
    channel.setProperty (ids::pan, stored (pan), nullptr);
}

void routeTo (juce::ValueTree channel, int mixerTrackId)
{
    if (channel.isValid())
        channel.setProperty (ids::mixerTrackId, mixerTrackId, nullptr);
}

} // namespace dew::demo
