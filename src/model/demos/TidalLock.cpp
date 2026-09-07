// =============================================================================
// Tidal Lock - Berlin-school sequencer music, 120 bpm, 5/4, G Lydian, 56 bars.
//
// A sixteenth sequence that does not divide the bar. Five beats to a bar means
// twenty steps, and a figure written in eights or sixteens phases against it -
// the pattern arrives a step earlier every bar and comes home after four. That
// is the whole compositional idea of the style, and it is why this is in five
// rather than in four with a clever delay.
//
// The bells are FM again, and deliberately unlike Late Rhodes's: there the
// modulator is an octave up and the result is an electric piano, here it is two
// octaves and a fifth up and the result is glass. Same three cells, same matrix,
// different interval - which is the thing a demo can show and a description
// cannot.
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

constexpr int kBeat = 4;
constexpr int kBar = kBeat * 5;
constexpr int kG2 = 43;

// G Lydian: G A B C# D E F#. The sharp fourth is the only note that separates
// it from G major, and the sequence hits it on every pass.
constexpr int kLydian[7] { 0, 2, 4, 6, 7, 9, 11 };

} // namespace

juce::ValueTree ProjectFactory::createTidalLock()
{
    auto project = scaffold (6);

    setSong (project, "Tidal Lock", 120.0, 56);
    setGrid (project, kBeat, 5, 4);

    const char* names[] { "Sequence", "Bass", "Bell", "Drone", "Perc", "Choir" };

    for (int i = 0; i < 6; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    // The sequence, with a vibrato locked to the tempo rather than to a number
    // of Hz. A free LFO drifts against a bar within four of them; a synced one
    // comes round with the eighths whatever the tempo curve does.
    auto seq = channelWithId (project, 1);
    setWavetableOsc (seq, 0, "pulse", 0.34, 0.20, "envelope", 0.5, 3, 8.0, 0, 0.62);
    setClassicOsc (seq, 1, "saw", -1, 0.30, -6);
    setSyncedLfo (seq, 0, "triangle", "eighth", 0.22, 0.0, 0.0);
    setAmp (seq, 0.002, 0.170, 0.28, 0.140);
    setMix (seq, 0.58, -0.14);

    auto bass = channelWithId (project, 2);
    setClassicOsc (bass, 0, "saw", -1, 0.72);
    setClassicOsc (bass, 1, "sine", -2, 0.60);
    setAmp (bass, 0.005, 0.320, 0.68, 0.130);
    setMix (bass, 0.78, 0.0);

    // Glass, not piano: the modulator is a fifth above two octaves up.
    auto bell = channelWithId (project, 3);
    setClassicOsc (bell, 0, "sine", 0, 0.55);
    setClassicOsc (bell, 1, "sine", 1, 0.62);
    setClassicOsc (bell, 2, "sine", 2, 0.70, 7);
    setFm (bell, 1, 0.0, 0.0, 0.34, 0.0);
    setFm (bell, 2, 0.0, 0.0, 0.0, 0.85);
    setAmp (bell, 0.003, 1.100, 0.08, 1.400);
    setMix (bell, 0.42, 0.24);

    auto drone = channelWithId (project, 4);
    setWavetableOsc (drone, 0, "formant", 0.28, 0.70, "lfo", 0.05, 5, 19.0, -1, 0.50);
    setAmp (drone, 1.600, 2.400, 0.78, 2.600);
    setMix (drone, 0.40, 0.0);

    auto perc = channelWithId (project, 5);
    setClassicOsc (perc, 0, "square", 2, 0.30, 29);
    setClassicOsc (perc, 1, "triangle", 1, 0.24);
    setAmp (perc, 0.001, 0.060, 0.0, 0.050);
    setMix (perc, 0.28, 0.30);

    auto choir = channelWithId (project, 6);
    setClassicOsc (choir, 0, "triangle", 0, 0.52, -8);
    setClassicOsc (choir, 1, "triangle", 0, 0.48, 9);
    setLfo (choir, 0, "sine", 0.13, 0.0, -0.18, 0.6);
    setAmp (choir, 0.700, 1.800, 0.66, 1.500);
    setMix (choir, 0.30, -0.26);

    setInserts (project, { "Sequence", "Bass", "Bell", "Drone", "Perc", "Choir" });

    for (int id = 1; id <= 6; ++id)
        routeTo (channelWithId (project, id), id);

    auto seqInsert = mixerTrackWithId (project, 1);
    seqInsert.appendChild (makeFilter (1, "lowpass", 2400.0, 1.8), nullptr);
    seqInsert.appendChild (
        makeEffect (2, "delay",
                    { { ids::delayMs, 375.0 }, { ids::feedback, 0.30 }, { ids::mix, 0.34 } }),
        nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (3, "eq",
                                  { { ids::lowGainDb, 2.5 },
                                    { ids::midGainDb, -1.0 },
                                    { ids::midFreq, 480.0 },
                                    { ids::highGainDb, -3.5 } }),
                      nullptr);

    // The one delay in the library whose feedback is a performance: it climbs
    // from a slap to nearly self-oscillating across the middle of the piece and
    // comes back, which is what a Berlin-school patch does with the knob.
    mixerTrackWithId (project, 3)
        .appendChild (
            makeEffect (4, "delay",
                        { { ids::delayMs, 750.0 }, { ids::feedback, 0.28 }, { ids::mix, 0.46 } }),
            nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (makeEffect (5, "reverb",
                                  { { ids::roomSize, 0.95 },
                                    { ids::damping, 0.26 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.52 } }),
                      nullptr);

    mixerTrackWithId (project, 5)
        .appendChild (
            makeEffect (6, "delay",
                        { { ids::delayMs, 250.0 }, { ids::feedback, 0.44 }, { ids::mix, 0.40 } }),
            nullptr);

    mixerTrackWithId (project, 6)
        .appendChild (makeEffect (7, "chorus",
                                  { { ids::rate, 0.22 }, { ids::depth, 0.58 }, { ids::mix, 0.5 } }),
                      nullptr);

    setMixerTrack (project, 1, 0.62, -0.08);
    setMixerTrack (project, 2, 0.74, 0.0);
    setMixerTrack (project, 3, 0.44, 0.16);
    setMixerTrack (project, 4, 0.50, 0.0);
    setMixerTrack (project, 5, 0.30, 0.24);
    setMixerTrack (project, 6, 0.34, -0.18);

    auto master = masterOf (project);
    master.appendChild (makeEffect (8, "eq",
                                    { { ids::lowGainDb, 0.5 },
                                      { ids::midGainDb, 0.0 },
                                      { ids::midFreq, 1000.0 },
                                      { ids::highGainDb, 1.5 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (1.30), nullptr);

    // --- patterns -------------------------------------------------------------
    // Four bars, and a sixteen-note figure inside twenty steps a bar. The
    // figure and the bar only agree every four bars, which is the phase.
    // Eighths, all the way through: forty of them across four bars of twenty
    // steps. The figure is eight notes long - sixteen steps - and the bar is
    // twenty, so it starts a beat earlier every bar and only comes home on the
    // fifth. Nothing about that is written down anywhere; it falls out of the
    // metre, which is the point.
    auto sequence = patternIn (project, 1, "Sequence", kBar * 4);
    const int shape[8] { 0, 4, 2, 6, 4, 2, 5, 3 };
    const auto eighths = kBar * 4 / 2;

    for (int i = 0; i < eighths; ++i)
    {
        const auto degree = shape[i % 8];
        const auto octave = (i % 16) >= 8 ? 12 : 0;
        sequence.appendChild (
            makeNote (1, i * 2, 2, kG2 + 12 + kLydian[degree] + octave, (i % 8) == 0 ? 0.72 : 0.5),
            nullptr);
    }

    auto bassline = patternIn (project, 2, "Bass", kBar * 4);
    const int roots[4] { kG2, kG2 + 4, kG2 + 9, kG2 + 2 };

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        notes (bassline, 2,
               { { at, kBeat * 2, roots[bar] - 12, 0.88 },
                 { at + kBeat * 2, kBeat, roots[bar] - 5, 0.7 },
                 { at + kBeat * 3, kBeat * 2, roots[bar] - 12, 0.8 } });
    }

    auto bells = patternIn (project, 3, "Bells", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        notes (bells, 3,
               { { at + kBeat, kBeat * 3, roots[bar] + 24, 0.6 },
                 { at + kBeat * 3, kBeat * 2, roots[bar] + 31, 0.52 } });
    }

    auto drones = patternIn (project, 4, "Drone", kBar * 4);
    notes (drones, 4, { { 0, kBar * 2, kG2 - 12, 0.6 }, { kBar * 2, kBar * 2, kG2 - 5, 0.56 } });

    auto percs = patternIn (project, 5, "Perc", kBar);
    steps (percs, 5, 88, "x...x...x...x...x...", 0.4, 2, 1);

    auto choirs = patternIn (project, 6, "Choir", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
        chord (choirs, 6, bar * kBar, kBar - 2,
               { roots[bar] + 12, roots[bar] + 16, roots[bar] + 19 }, 0.5);

    // --- automation -----------------------------------------------------------
    project.appendChild (
        makeAutomation (1, AutomationScope::mixerEffect, 3, 0, ids::feedback,
                        { { 0.0, curveValueIn ("delay", ids::feedback, 0.22) },
                          { kBar * 12.0, curveValueIn ("delay", ids::feedback, 0.86), -0.45 },
                          { kBar * 24.0, curveValueIn ("delay", ids::feedback, 0.30), 0.35 } }),
        nullptr);

    project.appendChild (
        makeAutomation (
            2, AutomationScope::channelOsc, 1, 0, ids::wavePosition,
            { { 0.0, 0.24 }, { kBar * 8.0, 0.78, -0.35 }, { kBar * 16.0, 0.40, 0.30 } }),
        nullptr);

    project.appendChild (
        makeAutomation (3, AutomationScope::mixerEffect, 1, 0, ids::cutoff,
                        { { 0.0, curveValueIn ("filter", ids::cutoff, 620.0) },
                          { kBar * 10.0, curveValueIn ("filter", ids::cutoff, 5200.0), -0.4 },
                          { kBar * 20.0, curveValueIn ("filter", ids::cutoff, 900.0), 0.3 } }),
        nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Sequence", "Bass", "Bells", "Drone", "Perc", "Choir", "Feedback", "Morph",
                         "Filter" });

    auto seqLane = laneAt (project, 0);
    seqLane.appendChild (makeClip (project, 1, 4, 28), nullptr);
    seqLane.appendChild (makeClip (project, 1, 36, 20), nullptr);

    auto bassLane = laneAt (project, 1);
    bassLane.appendChild (makeClip (project, 2, 8, 24), nullptr);
    bassLane.appendChild (makeClip (project, 2, 36, 16), nullptr);

    auto bellLane = laneAt (project, 2);
    bellLane.appendChild (makeClip (project, 3, 16, 16), nullptr);
    bellLane.appendChild (makeClip (project, 3, 40, 16), nullptr);

    auto droneLane = laneAt (project, 3);
    droneLane.appendChild (makeClip (project, 4, 0, 24), nullptr);
    droneLane.appendChild (makeClip (project, 4, 32, 24), nullptr);

    auto percLane = laneAt (project, 4);
    percLane.appendChild (makeClip (project, 5, 12, 20), nullptr);
    percLane.appendChild (makeClip (project, 5, 40, 12), nullptr);

    auto choirLane = laneAt (project, 5);
    choirLane.appendChild (makeClip (project, 6, 20, 12), nullptr);
    choirLane.appendChild (makeClip (project, 6, 44, 12), nullptr);

    laneAt (project, 6).appendChild (makeAutomationClip (project, 1, 8, 32), nullptr);
    laneAt (project, 7).appendChild (makeAutomationClip (project, 2, 4, 20), nullptr);
    laneAt (project, 8).appendChild (makeAutomationClip (project, 3, 4, 24), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
