#pragma once

// =============================================================================
// The fixtures two LFO test files share.
//
// A header rather than a copy in each, for the reason SourceScan.h gives about
// the suite: a helper written out twice is a helper the two files learn to
// disagree about, and `withLfo` computing `lfoActive` the way the reader does
// is exactly the sort of detail that would drift.
// =============================================================================

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "engine/EngineSnapshot.h"
#include "engine/SynthChannel.h"

namespace dewtest
{

using namespace dew;

inline OscSettings classicSlot (Waveform wave = Waveform::saw, float gain = 0.8f)
{
    OscSettings s;
    s.enabled = true;
    s.wave = wave;
    s.gain = gain;
    return s;
}

/** A slot whose LFO is on and asking for something. `lfoActive` is what the
    reader computes; a test that set the depths and forgot it would be testing
    the plain pass and passing for the wrong reason. */
inline OscSettings withLfo (OscSettings s, float toPitch, float toVolume, float toPan,
                            float hz = 5.0f)
{
    s.lfoOn = true;
    s.lfoHz = hz;
    s.lfoToPitch = toPitch;
    s.lfoToVolume = toVolume;
    s.lfoToPan = toPan;
    s.lfoActive = ! (juce::exactlyEqual (toPitch, 0.0f) && juce::exactlyEqual (toVolume, 0.0f)
                     && juce::exactlyEqual (toPan, 0.0f));
    return s;
}

inline OscBankSnapshot bankOf (std::initializer_list<OscSettings> enabledSlots)
{
    OscBankSnapshot bank;

    for (const auto& slot : enabledSlots)
        bank.slots[(size_t) bank.numSlots++] = slot;

    while (bank.numSlots < kMaxOscillators)
        bank.slots[(size_t) bank.numSlots++].enabled = false;

    bank.anyEnabled = ! std::empty (enabledSlots);
    return bank;
}

inline AmpSettings flatAmp()
{
    AmpSettings amp;
    amp.attack = 0.0f;
    amp.decay = 0.0f;
    amp.sustain = 1.0f;
    amp.release = 0.0f;
    return amp;
}

/** One note through a real channel, mono - the call every pinned oscillator
    test in this repository already makes. */
inline juce::AudioBuffer<float> renderMono (const OscBankSnapshot& bank, int numSamples = 8192)
{
    SynthChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (60, 1.0f, bank, flatAmp(), numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    channel.renderAdd (buffer.getWritePointer (0), numSamples);

    return buffer;
}

/** One note, rendered in the SIZE OF BLOCK the engine actually uses.

    It matters for pitch and for nothing else. Level and pan are evaluated per
    sample, but the pitch fold is per block - the same cadence the mod wheel's
    vibrato has always run at - so a test that asked for one enormous block
    would sample the LFO exactly once, at phase zero, and conclude that pitch
    modulation does nothing.
*/
inline juce::AudioBuffer<float> renderInBlocks (const OscBankSnapshot& bank, int numSamples = 16384,
                                                int blockSize = 256)
{
    SynthChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (60, 1.0f, bank, flatAmp(), numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();

    for (int i = 0; i < numSamples; i += blockSize)
    {
        const auto n = juce::jmin (blockSize, numSamples - i);
        channel.renderAdd (buffer.getWritePointer (0) + i, n);
    }

    return buffer;
}

/** One note in real blocks, with a LIVE bank handed to every block - and the
    chance to swap that bank part-way, which is what an automation curve or a
    turned knob does to a note already sounding.

    `swapAt` is a sample index; from the block containing it onwards, `after` is
    the bank the render sees. Pass the same bank twice for the control run.
*/
inline juce::AudioBuffer<float> renderLive (const OscBankSnapshot& before,
                                            const OscBankSnapshot& after, int swapAt,
                                            int numSamples = 16384, int blockSize = 256)
{
    SynthChannel channel;
    channel.prepare (44100.0);
    channel.noteOn (60, 1.0f, before, flatAmp(), numSamples * 2);

    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();

    for (int i = 0; i < numSamples; i += blockSize)
    {
        const auto n = juce::jmin (blockSize, numSamples - i);
        const auto& live = i >= swapAt ? after : before;

        channel.renderAdd (buffer.getWritePointer (0) + i, n, 0.0f, 0.0f, &live);
    }

    return buffer;
}

} // namespace dewtest
