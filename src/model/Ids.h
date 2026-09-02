#pragma once

#include <juce_data_structures/juce_data_structures.h>

/** Identifiers for the project ValueTree and its JSON form.

    The tree mirrors the JSON structure exactly - every node type corresponds to
    either a JSON object or an element of a JSON array - so the mapping in
    ProjectSchema stays a straight walk with no special cases.

        PROJECT
          CHANNEL*                     -> "channels": []
            INSTRUMENT                 -> "instrument": {}
              OSC                      -> "osc": {}
              AMP                      -> "amp": {}
          PATTERN*                     -> "patterns": []
            NOTE*                      -> "notes": []
          PLAYLIST                     -> "playlist": {}
            PLAYLIST_TRACK*            -> "tracks": []
              CLIP*                    -> "clips": []
          MIXER                        -> "mixer": {}
            MASTER                     -> "master": {}
            MIXER_TRACK*               -> "tracks": []
*/
namespace dew::ids
{

#define DEW_DECLARE_ID(name) const juce::Identifier name (#name);

// --- node types --------------------------------------------------------------
DEW_DECLARE_ID (PROJECT)
DEW_DECLARE_ID (CHANNEL)
DEW_DECLARE_ID (INSTRUMENT)
DEW_DECLARE_ID (OSC)
DEW_DECLARE_ID (AMP)
DEW_DECLARE_ID (PATTERN)
DEW_DECLARE_ID (NOTE)
DEW_DECLARE_ID (PLAYLIST)
DEW_DECLARE_ID (PLAYLIST_TRACK)
DEW_DECLARE_ID (CLIP)
DEW_DECLARE_ID (MIXER)
DEW_DECLARE_ID (MASTER)
DEW_DECLARE_ID (MIXER_TRACK)

// --- properties --------------------------------------------------------------
DEW_DECLARE_ID (formatVersion)
DEW_DECLARE_ID (name)
DEW_DECLARE_ID (tempoBpm)
DEW_DECLARE_ID (stepsPerBeat)
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

DEW_DECLARE_ID (mute)
DEW_DECLARE_ID (solo)

#undef DEW_DECLARE_ID

} // namespace dew::ids
