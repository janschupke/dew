#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "lang/Harmony.h"
#include "lang/Melody.h"
#include "lang/Music.h"
#include "lang/Voicing.h"

/** The four builders the score generator's tests are written in.

    Shared by the harmony, voicing and melody tests, which all need a chord, a
    harmony over it or a rhythm under it before they can say anything.

    JUCE-free, like everything else in this layer: these three files build into
    dew_lang_tests as well as dew_tests, which is what puts them in front of a
    second compiler and a second standard library.
*/
namespace dew::testing
{

using namespace dew::lang;

inline ChordSpec chordFrom (std::string_view root, int weight = 0, int bars = 0)
{
    ChordSpec chord;
    chord.symbol = { root, 0, {} };

    if (weight > 0)
    {
        chord.hasWeight = true;
        chord.weight = weight;
    }

    if (bars > 0)
        chord.bars = bars;

    return chord;
}

inline HarmonySpec harmonyOf (std::vector<ChordSpec> chords, std::optional<Key> key = {})
{
    HarmonySpec harmony;
    harmony.name = "h";
    harmony.key = key;
    harmony.chords = std::move (chords);
    return harmony;
}

inline std::vector<int> lengthsOf (const std::vector<ChordSpan>& spans)
{
    std::vector<int> lengths;

    for (const auto& span : spans)
        lengths.push_back (span.endStep - span.startStep);

    return lengths;
}

inline RhythmSpec rhythmOf (std::vector<Duration> durations)
{
    RhythmSpec rhythm;
    rhythm.name = "r";

    for (const auto duration : durations)
        rhythm.steps.push_back ({ duration, false, false, {} });

    return rhythm;
}

} // namespace dew::testing
