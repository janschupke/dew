#pragma once

#include <juce_data_structures/juce_data_structures.h>

/** Identifiers for the project ValueTree and its JSON form.

    The tree mirrors the JSON structure exactly - every node type corresponds to
    either a JSON object or an element of a JSON array - so the mapping in
    ProjectSchema stays a straight walk with no special cases.

        PROJECT
          CHANNEL*                     -> "channels": []
            INSTRUMENT                 -> "instrument": {}
              OSC (x kMaxOscillators)  -> "oscillators": []
              AMP                      -> "amp": {}
            SAMPLE                     -> "sample": {}
            EFFECT*                    -> "effects": []
          PATTERN*                     -> "patterns": []
            NOTE*                      -> "notes": []
          AUTOMATION*                  -> "automations": []
            POINT*                     -> "points": []
          PLAYLIST                     -> "playlist": {}
            PLAYLIST_TRACK*            -> "tracks": []
              CLIP*                    -> "clips": []
          MIXER                        -> "mixer": {}
            MASTER                     -> "master": {}
            MIXER_TRACK*               -> "tracks": []
              EFFECT*                  -> "effects": []
          SCORE                        -> "score": {}
            LINE*                      -> "lines": []
*/
namespace dew::ids
{

/** An identifier, declared once for the whole program.

    `inline` is load-bearing, not decoration. A namespace-scope `const` has
    INTERNAL linkage in C++, so without it every translation unit that includes
    this header gets its own copy of all eighty-seven Identifiers plus its own
    dynamic initialiser for each - and this header reaches most of src/ and all
    of tests/.
*/
#define DEW_DECLARE_ID(name) inline const juce::Identifier name (#name);

// --- node types --------------------------------------------------------------
DEW_DECLARE_ID (PROJECT)
DEW_DECLARE_ID (CHANNEL)
DEW_DECLARE_ID (INSTRUMENT)
DEW_DECLARE_ID (OSC)
DEW_DECLARE_ID (AMP)
DEW_DECLARE_ID (SAMPLE)
DEW_DECLARE_ID (PATTERN)
DEW_DECLARE_ID (NOTE)
DEW_DECLARE_ID (PLAYLIST)
DEW_DECLARE_ID (PLAYLIST_TRACK)
DEW_DECLARE_ID (CLIP)
DEW_DECLARE_ID (MIXER)
DEW_DECLARE_ID (MASTER)
DEW_DECLARE_ID (MIXER_TRACK)
DEW_DECLARE_ID (EFFECT)
DEW_DECLARE_ID (AUTOMATION)
DEW_DECLARE_ID (POINT)
DEW_DECLARE_ID (SCORE)
DEW_DECLARE_ID (LINE)

// --- properties --------------------------------------------------------------
DEW_DECLARE_ID (formatVersion)
DEW_DECLARE_ID (name)
DEW_DECLARE_ID (tempoBpm)
DEW_DECLARE_ID (stepsPerBeat)
DEW_DECLARE_ID (beatsPerBar)
DEW_DECLARE_ID (beatUnit)
DEW_DECLARE_ID (barsInSong)

DEW_DECLARE_ID (id)
DEW_DECLARE_ID (colour)
DEW_DECLARE_ID (mixerTrackId)
DEW_DECLARE_ID (basePitch)
DEW_DECLARE_ID (volume)
DEW_DECLARE_ID (pan)
DEW_DECLARE_ID (muted)

DEW_DECLARE_ID (wave)
DEW_DECLARE_ID (octave)
DEW_DECLARE_ID (detuneCents)
DEW_DECLARE_ID (gain)

// An oscillator slot in "wavetable" mode reads these and ignores `wave`; one in
// "classic" mode does the reverse. Both sets live on the same node, the way
// every effect type's parameters share one EFFECT node.
DEW_DECLARE_ID (mode)
DEW_DECLARE_ID (wavetable)
DEW_DECLARE_ID (wavePosition)
DEW_DECLARE_ID (wavePositionMod)
DEW_DECLARE_ID (wavePositionSource)
DEW_DECLARE_ID (wavePositionRate)
DEW_DECLARE_ID (unisonVoices)
DEW_DECLARE_ID (unisonDetune)

DEW_DECLARE_ID (attack)
DEW_DECLARE_ID (decay)
DEW_DECLARE_ID (sustain)
DEW_DECLARE_ID (release)

DEW_DECLARE_ID (lengthSteps)
DEW_DECLARE_ID (ch)
DEW_DECLARE_ID (step)
DEW_DECLARE_ID (pitch)
DEW_DECLARE_ID (velocity)

DEW_DECLARE_ID (patternId)
DEW_DECLARE_ID (startBar)
DEW_DECLARE_ID (lengthBars)
DEW_DECLARE_ID (kind)
DEW_DECLARE_ID (automationId)
DEW_DECLARE_ID (channelId)

// --- audio channels ----------------------------------------------------------
DEW_DECLARE_ID (source)
DEW_DECLARE_ID (file)
DEW_DECLARE_ID (sourceSampleRate)
DEW_DECLARE_ID (lengthSamples)
DEW_DECLARE_ID (startSample)
DEW_DECLARE_ID (endSample)
DEW_DECLARE_ID (fadeInMs)
DEW_DECLARE_ID (fadeOutMs)
DEW_DECLARE_ID (transpose)
DEW_DECLARE_ID (reverse)
DEW_DECLARE_ID (loop)

// --- automation --------------------------------------------------------------
DEW_DECLARE_ID (scope)
DEW_DECLARE_ID (targetId)
DEW_DECLARE_ID (slot)
DEW_DECLARE_ID (param)
DEW_DECLARE_ID (value)
DEW_DECLARE_ID (curve)
// What the segment to a point's RIGHT does: "curve" or "step". Beside `curve`
// rather than replacing it, because the two say different things - the shape is
// what kind of segment it is, the curve is how far it bends.
DEW_DECLARE_ID (shape)

DEW_DECLARE_ID (mute)
DEW_DECLARE_ID (solo)

// --- the score language ------------------------------------------------------
// One property per line of source, rather than one blob: juce::JSON escapes a
// newline as \n, so a score stored as a single string turns every edit into one
// enormous changed line in a file that tests and people both read as a diff.
DEW_DECLARE_ID (text)

// Which compiled node this is, and what it held when it was written. Empty on
// everything a person made - that emptiness IS the ownership line, and it is
// what lets a recompile replace its own work without touching anyone else's.
DEW_DECLARE_ID (genId)
DEW_DECLARE_ID (genHash)

// --- effects -----------------------------------------------------------------
DEW_DECLARE_ID (type)
DEW_DECLARE_ID (enabled)
DEW_DECLARE_ID (mix)
DEW_DECLARE_ID (filterMode)
DEW_DECLARE_ID (cutoff)
DEW_DECLARE_ID (resonance)
DEW_DECLARE_ID (roomSize)
DEW_DECLARE_ID (damping)
DEW_DECLARE_ID (width)
DEW_DECLARE_ID (delayMs)
DEW_DECLARE_ID (feedback)
DEW_DECLARE_ID (drive)
DEW_DECLARE_ID (outputGain)
DEW_DECLARE_ID (rate)
DEW_DECLARE_ID (depth)
DEW_DECLARE_ID (lowGainDb)
DEW_DECLARE_ID (midGainDb)
DEW_DECLARE_ID (midFreq)
DEW_DECLARE_ID (highGainDb)

#undef DEW_DECLARE_ID

} // namespace dew::ids
