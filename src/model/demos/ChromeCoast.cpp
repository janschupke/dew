// =============================================================================
// Chrome Coast - synthwave, 108 bpm, 4/4, A Aeolian with a Lydian lift, 48 bars.
//
// Saw stacks, a gated pad and a delay whose time is DERIVED from the tempo
// rather than dialled by ear: a dotted eighth at 108 is 416.7 ms, and writing
// the arithmetic down is the difference between a delay that locks to the
// eighths and one that is nearly right.
//
// Two things here exist nowhere else in the library. The pad's wavetable
// position STEPS between two frames on the chorus instead of sliding, because a
// gate is an event and a ramp is a motion - dew stores both shapes and every
// shipped curve had been a slide. And one lane is MUTED: a second lead line
// that was tried and kept, which is what a lane mute is actually for and what
// no committed project had ever contained.
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

constexpr int kBar = 16;

/** A dotted eighth at this tempo, in milliseconds. */
constexpr double dottedEighthMs (double bpm)
{
    return 60000.0 / bpm * 0.75;
}

} // namespace

juce::ValueTree ProjectFactory::createChromeCoast()
{
    auto project = scaffold (7);

    setSong (project, "Chrome Coast", 108.0, 48);
    setGrid (project, 4, 4, 4);

    const char* names[] { "Kick", "Snare", "Hat", "Bass", "Pad", "Lead", "Arp" };

    for (int i = 0; i < 7; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    auto kick = channelWithId (project, 1);
    setClassicOsc (kick, 0, "sine", -1, 1.0);
    setAmp (kick, 0.001, 0.190, 0.0, 0.070);
    setMix (kick, 0.88, 0.0);

    auto snare = channelWithId (project, 2);
    setClassicOsc (snare, 0, "square", 0, 0.44);
    setClassicOsc (snare, 1, "saw", 1, 0.42, 27);
    setAmp (snare, 0.002, 0.190, 0.0, 0.180);
    setMix (snare, 0.60, 0.0);

    auto hat = channelWithId (project, 3);
    setClassicOsc (hat, 0, "square", 2, 0.32, 19);
    setAmp (hat, 0.001, 0.040, 0.0, 0.036);
    setMix (hat, 0.28, 0.20);

    auto bass = channelWithId (project, 4);
    setClassicOsc (bass, 0, "saw", 0, 0.68, -6);
    setClassicOsc (bass, 1, "saw", 0, 0.62, 7);
    setClassicOsc (bass, 2, "sine", -1, 0.55);
    setAmp (bass, 0.004, 0.260, 0.72, 0.100);
    setMix (bass, 0.80, 0.0);

    // The gate. `basic` frames, and a position that is written as steps rather
    // than as a ramp - see the curve below.
    auto pad = channelWithId (project, 5);
    setWavetableOsc (pad, 0, "basic", 0.20, 0.0, "envelope", 0.5, 5, 15.0, 0, 0.58);
    setWavetableOsc (pad, 1, "pulse", 0.62, 0.25, "lfo", 0.16, 3, 21.0, -1, 0.36);
    setAmp (pad, 0.240, 1.100, 0.62, 0.900);
    setMix (pad, 0.42, -0.18);

    auto lead = channelWithId (project, 6);
    setClassicOsc (lead, 0, "saw", 0, 0.62, -11);
    setClassicOsc (lead, 1, "saw", 0, 0.62, 12);
    setClassicOsc (lead, 2, "triangle", 1, 0.30);
    setAmp (lead, 0.020, 0.420, 0.70, 0.340);
    setMix (lead, 0.52, 0.10);

    auto arp = channelWithId (project, 7);
    setClassicOsc (arp, 0, "square", 1, 0.42, -4);
    setClassicOsc (arp, 1, "triangle", 1, 0.30, 5);
    setAmp (arp, 0.002, 0.140, 0.24, 0.120);
    setMix (arp, 0.34, 0.26);

    setInserts (project, { "Drums", "Bass", "Pad", "Lead", "Arp" });

    for (int id = 1; id <= 3; ++id)
        routeTo (channelWithId (project, id), 1);

    routeTo (bass, 2);
    routeTo (pad, 3);
    routeTo (lead, 4);
    routeTo (arp, 5);

    mixerTrackWithId (project, 1)
        .appendChild (makeEffect (1, "reverb",
                                  { { ids::roomSize, 0.72 },
                                    { ids::damping, 0.44 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.22 } }),
                      nullptr);

    mixerTrackWithId (project, 2).appendChild (makeFilter (2, "lowpass", 2200.0, 0.8), nullptr);

    auto padInsert = mixerTrackWithId (project, 3);
    padInsert.appendChild (
        makeEffect (3, "chorus", { { ids::rate, 0.34 }, { ids::depth, 0.62 }, { ids::mix, 0.55 } }),
        nullptr);
    padInsert.appendChild (makeEffect (4, "reverb",
                                       { { ids::roomSize, 0.90 },
                                         { ids::damping, 0.30 },
                                         { ids::width, 1.0 },
                                         { ids::mix, 0.48 } }),
                           nullptr);

    auto leadInsert = mixerTrackWithId (project, 4);
    leadInsert.appendChild (makeEffect (5, "delay",
                                        { { ids::delayMs, dottedEighthMs (108.0) },
                                          { ids::feedback, 0.44 },
                                          { ids::mix, 0.34 } }),
                            nullptr);
    leadInsert.appendChild (
        makeEffect (6, "chorus", { { ids::rate, 0.9 }, { ids::depth, 0.30 }, { ids::mix, 0.40 } }),
        nullptr);

    mixerTrackWithId (project, 5)
        .appendChild (makeEffect (7, "delay",
                                  { { ids::delayMs, dottedEighthMs (108.0) },
                                    { ids::feedback, 0.32 },
                                    { ids::mix, 0.30 } }),
                      nullptr);

    setMixerTrack (project, 1, 0.78, 0.0);
    setMixerTrack (project, 2, 0.74, 0.0);
    setMixerTrack (project, 3, 0.48, -0.12);
    setMixerTrack (project, 4, 0.56, 0.08);
    setMixerTrack (project, 5, 0.38, 0.22);

    auto master = masterOf (project);
    master.appendChild (makeEffect (8, "eq",
                                    { { ids::lowGainDb, 2.0 },
                                      { ids::midGainDb, -1.5 },
                                      { ids::midFreq, 600.0 },
                                      { ids::highGainDb, 3.0 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (0.62), nullptr);

    // --- patterns -------------------------------------------------------------
    auto beat = patternIn (project, 1, "Beat", kBar);
    steps (beat, 1, 33, "x.......x.......", 0.9, 6);
    steps (beat, 2, 62, "....x.......x...", 0.82, 5);
    steps (beat, 3, 90, "..x...x...x...x.", 0.3, 1);

    auto beatB = patternIn (project, 2, "Beat B", kBar);
    steps (beatB, 1, 33, "x.....x.x.......", 0.9, 6);
    steps (beatB, 2, 62, "....x.......x..o", 0.82, 5);
    steps (beatB, 3, 90, "x.x.x.x.x.x.x.xx", 0.3, 1);

    // Am - F - C - G, four bars: the Aeolian home and the major three above it,
    // which is the whole synthwave cadence.
    const int roots[4] { 33, 29, 36, 31 };
    const int voicings[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };

    auto bassline = patternIn (project, 3, "Bass", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
        steps (bassline, 4, roots[bar], "x.x.x.x.x.x.x.x.", 0.82, 2, 1, bar * kBar);

    auto pads = patternIn (project, 4, "Pad", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
        chord (pads, 5, bar * kBar, kBar - 1,
               { voicings[bar][0], voicings[bar][1], voicings[bar][2] }, 0.54);

    auto arps = patternIn (project, 5, "Arp", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
        for (int i = 0; i < 8; ++i)
            arps.appendChild (
                makeNote (7, bar * kBar + i * 2, 2, voicings[bar][i % 3] + (i >= 6 ? 12 : 0), 0.42),
                nullptr);

    auto leadline = patternIn (project, 6, "Lead", kBar * 4);
    notes (leadline, 6,
           { { 0, 12, 69, 0.72 },
             { 14, 6, 72, 0.66 },
             { 22, 10, 76, 0.74 },
             { 34, 8, 74, 0.68 },
             { 44, 4, 72, 0.6 },
             { 48, 14, 69, 0.7 } });

    // The lane that is kept and silenced: the same phrase a fourth up, tried
    // and not used.
    auto alt = patternIn (project, 7, "Lead alt", kBar * 4);
    notes (alt, 6,
           { { 0, 12, 74, 0.68 },
             { 14, 6, 77, 0.62 },
             { 22, 10, 81, 0.7 },
             { 34, 8, 79, 0.64 },
             { 44, 4, 77, 0.58 },
             { 48, 14, 74, 0.66 } });

    // --- automation -----------------------------------------------------------
    // STEP, not slide. The pad's frame changes on the bar and stays there; a
    // ramp between the same two values is a different effect entirely, and the
    // library had only ever shipped the ramp.
    project.appendChild (makeAutomation (1, AutomationScope::channelOsc, 5, 0, ids::wavePosition,
                                         { { 0.0, 0.20, 0.0, "step" },
                                           { kBar * 1.0, 0.68, 0.0, "step" },
                                           { kBar * 2.0, 0.34, 0.0, "step" },
                                           { kBar * 3.0, 0.82, 0.0, "step" } }),
                         nullptr);

    project.appendChild (
        makeAutomation (2, AutomationScope::mixerEffect, 2, 0, ids::cutoff,
                        { { 0.0, curveValueIn ("filter", ids::cutoff, 700.0) },
                          { kBar * 16.0, curveValueIn ("filter", ids::cutoff, 4200.0), -0.35 } }),
        nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Drums", "Bass", "Pad", "Arp", "Lead", "Lead alt", "Gate", "Filter" });

    auto drums = laneAt (project, 0);
    drums.appendChild (makeClip (project, 1, 8, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 16, 16), nullptr);
    drums.appendChild (makeClip (project, 1, 32, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 40, 8), nullptr);

    laneAt (project, 1).appendChild (makeClip (project, 3, 8, 40), nullptr);

    auto padLane = laneAt (project, 2);
    padLane.appendChild (makeClip (project, 4, 0, 32), nullptr);
    padLane.appendChild (makeClip (project, 4, 36, 12), nullptr);

    auto arpLane = laneAt (project, 3);
    arpLane.appendChild (makeClip (project, 5, 16, 16), nullptr);
    arpLane.appendChild (makeClip (project, 5, 40, 8), nullptr);

    auto leadLane = laneAt (project, 4);
    leadLane.appendChild (makeClip (project, 6, 16, 8), nullptr);
    leadLane.appendChild (makeClip (project, 6, 32, 12), nullptr);

    // Same bars, muted. Un-mute it and the two leads play together, which is
    // how the idea was rejected in the first place.
    laneAt (project, 5).appendChild (makeClip (project, 7, 32, 12), nullptr);
    setLane (project, 5, true, 0.7);

    laneAt (project, 6).appendChild (makeAutomationClip (project, 1, 0, 4), nullptr);
    laneAt (project, 6).appendChild (makeAutomationClip (project, 1, 16, 16), nullptr);
    laneAt (project, 7).appendChild (makeAutomationClip (project, 2, 0, 16), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
