#pragma once

#include <juce_core/juce_core.h>

namespace dew
{

/** What a channel gets its samples from.

    In the model rather than the engine for the reason EffectType is: the
    schema, the preset library and the instrument panel all need to name a
    kind of instrument, and none of them should have to link the DSP to do it.

    It lived in EngineSnapshot until presets needed it, which meant the only
    code that could say whether a stored `source` string was legal was code
    that also had to be able to render it.
*/
enum class InstrumentType { synth, audio };

inline constexpr int kNumInstrumentTypes = 2;

/** An oscillator slot's closed string enums, here for the same reason
    FilterMode sits beside EffectType: they are the vocabulary of the file, not
    of the renderer, and something has to be able to reject "sawtooth" without
    building a wavetable to find out.
*/
enum class Waveform { sine, saw, square, triangle };

/** Which generator an oscillator slot runs.

    A property of the SLOT rather than of the channel, so a wavetable can be
    layered under a classic saw without the two being different instruments.
*/
enum class OscMode { classic, wavetable };

/** What drives a wavetable slot's position over the length of a note. */
enum class PositionSource { envelope, lfo };

Waveform waveformFromString (const juce::String&);
juce::String waveformToString (Waveform);

OscMode oscModeFromString (const juce::String&);
juce::String oscModeToString (OscMode);

PositionSource positionSourceFromString (const juce::String&);
juce::String positionSourceToString (PositionSource);

} // namespace dew
