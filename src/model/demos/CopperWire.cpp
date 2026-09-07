// =============================================================================
// Copper Wire - UK garage, 2-step, 134 bpm, 4/4, F# Dorian, 48 bars.
//
// The kick leaves the third beat empty and lands on the back half of it, the
// snare keeps two and four, and everything between them is swung. That swing is
// the reason this demo exists at TWELVE steps to a beat: dew has no swing knob,
// a step grid is exactly as swung as its divisions allow, and twelve is the
// smallest grid that holds both a sixteenth (three steps) and a triplet eighth
// (four). The offbeats below sit at eight steps into a twelve-step beat, which
// is a real shuffle rather than a straight line called one.
//
// Dorian rather than natural minor: the raised sixth is what stops a garage
// progression sounding like a lament, and it is the only note that separates
// the two.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

namespace
{

constexpr int kBeat = 12; // steps to a beat
constexpr int kBar = kBeat * 4;
constexpr int kSwing = 8; // where the offbeat eighth lands inside a beat

} // namespace

juce::ValueTree ProjectFactory::createCopperWire()
{
    auto project = scaffold (7);

    setSong (project, "Copper Wire", 134.0, 48);
    setGrid (project, kBeat, 4, 4);

    const char* names[] { "Kick", "Snare", "Hat", "Shaker", "Bass", "Organ", "Vox" };

    for (int i = 0; i < 7; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    auto kick = channelWithId (project, 1);
    setClassicOsc (kick, 0, "sine", -1, 1.0);
    setAmp (kick, 0.001, 0.150, 0.0, 0.060);
    setMix (kick, 0.92, 0.0);

    auto snare = channelWithId (project, 2);
    setClassicOsc (snare, 0, "square", 0, 0.45);
    setClassicOsc (snare, 1, "saw", 1, 0.40, 29);
    setAmp (snare, 0.001, 0.095, 0.0, 0.080);
    setMix (snare, 0.62, -0.05);

    // The hat breathes: an LFO locked to the tempo, taking a sixteenth to come
    // round, pulling the level down and back. A free-running one at some Hz
    // would drift against the bar within four of them.
    auto hat = channelWithId (project, 3);
    setClassicOsc (hat, 0, "square", 2, 0.35);
    setSyncedLfo (hat, 0, "triangle", "sixteenth", 0.0, -0.35, 0.0);
    setAmp (hat, 0.001, 0.030, 0.0, 0.026);
    setMix (hat, 0.34, 0.22);

    auto shaker = channelWithId (project, 4);
    setClassicOsc (shaker, 0, "square", 2, 0.30, 21);
    setAmp (shaker, 0.002, 0.040, 0.0, 0.035);
    setMix (shaker, 0.26, -0.30);

    auto bass = channelWithId (project, 5);
    setClassicOsc (bass, 0, "sine", 0, 0.85);
    setClassicOsc (bass, 1, "triangle", 0, 0.35, -7);
    setAmp (bass, 0.004, 0.190, 0.62, 0.110);
    setMix (bass, 0.82, 0.0);

    // A pulse wavetable is the organ: the position IS the pulse width, and a
    // narrow one is the hollow reedy thing a garage stab wants.
    auto organ = channelWithId (project, 6);
    setWavetableOsc (organ, 0, "pulse", 0.28, 0.0, "envelope", 0.4, 3, 7.0, 0, 0.65);
    setClassicOsc (organ, 1, "square", -1, 0.25);
    setAmp (organ, 0.005, 0.180, 0.30, 0.130);
    setMix (organ, 0.48, 0.08);

    auto vox = channelWithId (project, 7);
    setClassicOsc (vox, 0, "triangle", 0, 0.55, -5);
    setClassicOsc (vox, 1, "triangle", 0, 0.45, 6);
    setAmp (vox, 0.030, 0.320, 0.55, 0.260);
    setMix (vox, 0.36, -0.10);

    setInserts (project, { "Drums", "Bass", "Organ", "Vox" });

    for (int id = 1; id <= 4; ++id)
        routeTo (channelWithId (project, id), 1);

    routeTo (bass, 2);
    routeTo (organ, 3);
    routeTo (vox, 4);

    mixerTrackWithId (project, 1)
        .appendChild (makeEffect (1, "compressor",
                                  { { ids::threshold, -14.0 },
                                    { ids::ratio, 3.5 },
                                    { ids::attackMs, 8.0 },
                                    { ids::releaseMs, 90.0 },
                                    { ids::makeup, 2.0 } }),
                      nullptr);

    mixerTrackWithId (project, 2).appendChild (makeFilter (2, "lowpass", 1400.0, 0.9), nullptr);

    // Bandpass, then a phaser: the stab has no low end and no top, and the
    // phaser is what moves inside the band that is left.
    auto organInsert = mixerTrackWithId (project, 3);
    organInsert.appendChild (makeFilter (3, "bandpass", 1100.0, 1.6), nullptr);
    organInsert.appendChild (makeEffect (4, "phaser",
                                         { { ids::rate, 0.24 },
                                           { ids::depth, 0.7 },
                                           { ids::centreFreq, 900.0 },
                                           { ids::feedback, 0.5 },
                                           { ids::mix, 0.6 } }),
                             nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (
            makeEffect (5, "delay",
                        { { ids::delayMs, 336.0 }, { ids::feedback, 0.42 }, { ids::mix, 0.32 } }),
            nullptr);

    setMixerTrack (project, 1, 0.80, 0.0);
    setMixerTrack (project, 2, 0.72, 0.0);
    setMixerTrack (project, 3, 0.54, 0.05);
    setMixerTrack (project, 4, 0.40, -0.08);

    auto master = masterOf (project);
    master.appendChild (makeEffect (6, "eq",
                                    { { ids::lowGainDb, 2.0 },
                                      { ids::midGainDb, -1.5 },
                                      { ids::midFreq, 700.0 },
                                      { ids::highGainDb, 2.5 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (0.72), nullptr);

    // --- patterns -------------------------------------------------------------
    // A character is worth `stride` steps, so a line of sixteenths is sixteen
    // characters three steps apart and a line of eighths is four characters
    // twelve apart. The offbeat lines repeat the same string at an offset of
    // eight, which is what makes them late - and is the whole shuffle.
    auto skip = patternIn (project, 1, "2-Step", kBar);
    steps (skip, 1, 30, "x.........x.....", 0.95, 6, 3);
    steps (skip, 2, 62, "....x.......x...", 0.8, 4, 3);
    steps (skip, 3, 90, "xxxx", 0.5, 2, kBeat);
    steps (skip, 3, 90, "xxxx", 0.32, 2, kBeat, kSwing);
    steps (skip, 4, 92, "..x...x...x...x.", 0.28, 2, 3);

    auto skipB = patternIn (project, 2, "2-Step B", kBar);
    steps (skipB, 1, 30, "x.......x.x.....", 0.95, 6, 3);
    steps (skipB, 2, 62, "....x.......x..o", 0.8, 4, 3);
    steps (skipB, 3, 90, "XxXx", 0.5, 2, kBeat);
    steps (skipB, 3, 90, "xxxx", 0.34, 2, kBeat, kSwing);
    steps (skipB, 4, 92, "..x.x.x...x.x.x.", 0.26, 2, 3);

    auto drop = patternIn (project, 3, "Break", kBar);
    steps (drop, 2, 62, "....x.......x...", 0.72, 4, 3);
    steps (drop, 4, 92, "x.x.x.x.x.x.x.x.", 0.24, 2, 3);

    // Four bars of F#m9 - Dmaj7 - Amaj7 - Bm7, all voiced inside one octave so
    // the raised sixth is the only voice that has to move.
    auto stabs = patternIn (project, 4, "Stabs", kBar * 4);

    const int voicings[4][4] {
        { 54, 61, 64, 68 }, { 50, 54, 57, 61 }, { 57, 61, 64, 68 }, { 54, 57, 59, 62 }
    };

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        const auto& voicing = voicings[bar];

        chord (stabs, 6, at + kSwing, 10, { voicing[0], voicing[1], voicing[2], voicing[3] }, 0.66);
        chord (stabs, 6, at + kBeat * 2, 8, { voicing[0], voicing[1], voicing[2] }, 0.5);
        chord (stabs, 6, at + kBeat * 3 + kSwing, 10, { voicing[1], voicing[2], voicing[3] }, 0.58);
    }

    auto bassline = patternIn (project, 5, "Bassline", kBar * 4);
    const int roots[4] { 30, 26, 33, 35 };

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        const auto root = roots[bar];

        notes (bassline, 5,
               { { at, 10, root, 0.92 },
                 { at + kBeat + kSwing, 6, root + 12, 0.66 },
                 { at + kBeat * 2, 10, root, 0.84 },
                 { at + kBeat * 3 + kSwing, 8, root + 7, 0.7 } });
    }

    auto pads = patternIn (project, 6, "Vox", kBar * 4);
    notes (pads, 7,
           { { 0, kBar - 6, 78, 0.5 },
             { kBar, kBar - 6, 74, 0.46 },
             { kBar * 2, kBar - 6, 76, 0.52 },
             { kBar * 3, kBar - 6, 73, 0.44 } });

    // --- automation -----------------------------------------------------------
    // The bass filter opens across the first sixteen bars and shuts again for
    // the break, which is the only movement this arrangement needs.
    project.appendChild (
        makeAutomation (1, AutomationScope::mixerEffect, 2, 0, ids::cutoff,
                        { { 0.0, curveValueIn ("filter", ids::cutoff, 380.0) },
                          { kBar * 16.0, curveValueIn ("filter", ids::cutoff, 3200.0), -0.4 } }),
        nullptr);

    project.appendChild (
        makeAutomation (
            2, AutomationScope::channel, 6, -1, ids::volume,
            { { 0.0, curveValue (AutomationScope::channel, ids::volume, 0.12) },
              { kBar * 8.0, curveValue (AutomationScope::channel, ids::volume, 0.48), 0.35 } }),
        nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Drums", "Bass", "Organ", "Vox", "Filter", "Swell" });

    auto drums = laneAt (project, 0);
    drums.appendChild (makeClip (project, 1, 0, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 8, 8), nullptr);
    drums.appendChild (makeClip (project, 3, 16, 4), nullptr);
    drums.appendChild (makeClip (project, 2, 20, 12), nullptr);
    drums.appendChild (makeClip (project, 1, 32, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 40, 8), nullptr);

    auto bassLane = laneAt (project, 1);
    bassLane.appendChild (makeClip (project, 5, 4, 12), nullptr);
    bassLane.appendChild (makeClip (project, 5, 20, 12), nullptr);
    bassLane.appendChild (makeClip (project, 5, 36, 12), nullptr);

    auto organLane = laneAt (project, 2);
    organLane.appendChild (makeClip (project, 4, 8, 8), nullptr);
    organLane.appendChild (makeClip (project, 4, 20, 12), nullptr);
    organLane.appendChild (makeClip (project, 4, 40, 8), nullptr);

    auto voxLane = laneAt (project, 3);
    voxLane.appendChild (makeClip (project, 6, 16, 4), nullptr);
    voxLane.appendChild (makeClip (project, 6, 32, 8), nullptr);

    laneAt (project, 4).appendChild (makeAutomationClip (project, 1, 0, 16), nullptr);
    laneAt (project, 5).appendChild (makeAutomationClip (project, 2, 0, 8), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
