// =============================================================================
// Sublevel - drum and bass, 174 bpm, 4/4, D minor, 64 bars.
//
// The Reese is the point. A Reese bass is two saws detuned far enough to beat
// against each other, and what makes a NEUROFUNK one move is feedback: an
// oscillator folded into its OWN phase, which is the diagonal of the FM matrix
// and the one cell nothing else in the library uses. Under a bandpass that
// sweeps, that is the whole sound - no sample, no wavetable.
//
// The break is written rather than sampled, so the kit is four channels on one
// insert with a compressor doing what a compressor does to a break. Three lanes
// come and go independently across sixty-four bars, which at 174 is a real
// arrangement rather than a loop with a filter on it.
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
constexpr int kD1 = 26;

} // namespace

juce::ValueTree ProjectFactory::createSublevel()
{
    auto project = scaffold (8);

    setSong (project, "Sublevel", 174.0, 64);
    setGrid (project, 4, 4, 4);

    const char* names[] { "Kick", "Snare", "Hat", "Ride", "Reese", "Sub", "Lead", "Pad" };

    for (int i = 0; i < 8; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    auto kick = channelWithId (project, 1);
    setClassicOsc (kick, 0, "sine", -1, 1.0);
    setAmp (kick, 0.001, 0.120, 0.0, 0.045);
    setMix (kick, 0.90, 0.0);

    auto snare = channelWithId (project, 2);
    setClassicOsc (snare, 0, "square", 0, 0.48);
    setClassicOsc (snare, 1, "saw", 1, 0.44, 41);
    setAmp (snare, 0.001, 0.090, 0.0, 0.070);
    setMix (snare, 0.66, -0.04);

    auto hat = channelWithId (project, 3);
    setClassicOsc (hat, 0, "square", 2, 0.36, 23);
    setAmp (hat, 0.001, 0.026, 0.0, 0.024);
    setMix (hat, 0.30, 0.22);

    auto ride = channelWithId (project, 4);
    setClassicOsc (ride, 0, "square", 2, 0.28, -31);
    setClassicOsc (ride, 1, "triangle", 3, 0.22, 17);
    setAmp (ride, 0.002, 0.180, 0.0, 0.160);
    setMix (ride, 0.24, -0.26);

    // The Reese. Two saws fourteen cents apart, and slot 1 folded back into
    // ITSELF at 0.22 - the diagonal of the matrix, which is feedback and is
    // where the growl comes from. A third saw an octave down keeps it a bass.
    auto reese = channelWithId (project, 5);
    setClassicOsc (reese, 0, "saw", 0, 0.72, -14);
    setClassicOsc (reese, 1, "saw", 0, 0.72, 15);
    setClassicOsc (reese, 2, "saw", -1, 0.40);
    setFm (reese, 0, 0.22, 0.0, 0.0, 1.0);
    setFm (reese, 1, 0.0, 0.14, 0.0, 1.0);
    setAmp (reese, 0.004, 0.400, 0.85, 0.120);
    setMix (reese, 0.72, 0.0);

    auto sub = channelWithId (project, 6);
    setClassicOsc (sub, 0, "sine", -1, 0.95);
    setAmp (sub, 0.006, 0.300, 0.90, 0.080);
    setMix (sub, 0.84, 0.0);

    auto lead = channelWithId (project, 7);
    setWavetableOsc (lead, 0, "fold", 0.42, 0.60, "envelope", 0.5, 5, 14.0, 1, 0.60);
    setAmp (lead, 0.003, 0.220, 0.30, 0.180);
    setMix (lead, 0.44, 0.14);

    auto pad = channelWithId (project, 8);
    setWavetableOsc (pad, 0, "harmonics", 0.24, 0.40, "lfo", 0.13, 5, 12.0, 0, 0.48);
    setAmp (pad, 0.500, 1.400, 0.60, 1.200);
    setMix (pad, 0.28, -0.16);

    // --- routing --------------------------------------------------------------
    setInserts (project, { "Break", "Reese", "Sub", "Lead", "Pad" });

    for (int id = 1; id <= 4; ++id)
        routeTo (channelWithId (project, id), 1);

    routeTo (reese, 2);
    routeTo (sub, 3);
    routeTo (lead, 4);
    routeTo (pad, 5);

    auto breakInsert = mixerTrackWithId (project, 1);
    breakInsert.appendChild (makeEffect (1, "compressor",
                                         { { ids::threshold, -20.0 },
                                           { ids::ratio, 6.0 },
                                           { ids::attackMs, 2.0 },
                                           { ids::releaseMs, 70.0 },
                                           { ids::makeup, 4.0 } }),
                             nullptr);
    breakInsert.appendChild (
        makeEffect (2, "drive", { { ids::drive, 2.8 }, { ids::outputGain, 0.68 } }), nullptr);

    // On the CHANNEL rather than on the insert, and that is a musical claim
    // rather than a filing decision: a Reese's filter is part of the
    // INSTRUMENT - it is the thing being played - while an insert is where the
    // mix happens. Only a channel chain can say that, and no shipped project
    // had ever put an effect on a channel at all.
    //
    // Bandpass, then crush: the filter is what sweeps, and the crush is what
    // makes the sweep sound torn rather than merely opened.
    reese.appendChild (makeFilter (3, "bandpass", 420.0, 2.2), nullptr);
    reese.appendChild (makeDistortion (4, "crush", 5.0, 0.45, 0.55, 0.42), nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (10, "eq",
                                  { { ids::lowGainDb, 1.5 },
                                    { ids::midGainDb, -1.5 },
                                    { ids::midFreq, 700.0 },
                                    { ids::highGainDb, 1.0 } }),
                      nullptr);

    mixerTrackWithId (project, 3).appendChild (makeFilter (5, "lowpass", 180.0, 0.6), nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (
            makeEffect (6, "delay",
                        { { ids::delayMs, 259.0 }, { ids::feedback, 0.40 }, { ids::mix, 0.26 } }),
            nullptr);

    mixerTrackWithId (project, 5)
        .appendChild (makeEffect (7, "reverb",
                                  { { ids::roomSize, 0.80 },
                                    { ids::damping, 0.40 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.55 } }),
                      nullptr);

    setMixerTrack (project, 1, 0.80, 0.0);
    setMixerTrack (project, 2, 0.62, 0.0);
    setMixerTrack (project, 3, 0.70, 0.0);
    setMixerTrack (project, 4, 0.46, 0.08);
    setMixerTrack (project, 5, 0.34, -0.10);

    auto master = masterOf (project);
    master.appendChild (makeEffect (8, "eq",
                                    { { ids::lowGainDb, 1.0 },
                                      { ids::midGainDb, -2.0 },
                                      { ids::midFreq, 400.0 },
                                      { ids::highGainDb, 2.0 } }),
                        nullptr);
    master.appendChild (makeEffect (9, "limiter", { { ids::ceiling, -1.0 } }), nullptr);
    master.setProperty (ids::gain, stored (1.00), nullptr);

    // --- patterns -------------------------------------------------------------
    auto intro = patternIn (project, 1, "Intro", kBar);
    steps (intro, 3, 92, "..x...x...x...x.", 0.3, 1);
    steps (intro, 4, 88, "x.......x.......", 0.24, 6);

    auto amen = patternIn (project, 2, "Break", kBar);
    steps (amen, 1, kD1, "x.........x.....", 0.95, 5);
    steps (amen, 2, 64, "....x......o.x..", 0.85, 3);
    steps (amen, 3, 92, "x.x.x.x.x.x.x.x.", 0.32, 1);
    steps (amen, 4, 88, "..x.....x.....x.", 0.26, 5);

    auto amenB = patternIn (project, 3, "Break B", kBar);
    steps (amenB, 1, kD1, "x....x....x.....", 0.95, 5);
    steps (amenB, 2, 64, "....x..o...x.x..", 0.85, 3);
    steps (amenB, 3, 92, "x.xox.x.xox.x.x.", 0.34, 1);
    steps (amenB, 4, 88, "......x.......x.", 0.28, 5);

    // Two bars of Dm - Bb - F - C roots, held long. A Reese is a sustained
    // note; the movement is the filter, not the part.
    auto reeseline = patternIn (project, 4, "Reese", kBar * 4);
    notes (reeseline, 5,
           { { 0, 24, kD1, 0.9 },
             { 26, 6, kD1 + 12, 0.7 },
             { 32, 24, kD1 - 4, 0.88 },
             { 58, 6, kD1 + 8, 0.66 } });
    notes (reeseline, 6, { { 0, 30, kD1 - 12, 0.85 }, { 32, 30, kD1 - 16, 0.85 } });

    auto leadline = patternIn (project, 5, "Lead", kBar * 4);
    notes (leadline, 7,
           { { 4, 4, 74, 0.72 },
             { 10, 2, 77, 0.6 },
             { 14, 6, 72, 0.68 },
             { 24, 8, 69, 0.7 },
             { 36, 4, 77, 0.74 },
             { 42, 2, 79, 0.62 },
             { 46, 10, 74, 0.7 },
             { 58, 6, 69, 0.6 } });

    auto pads = patternIn (project, 6, "Pad", kBar * 4);
    chord (pads, 8, 0, 30, { 62, 65, 69 }, 0.5);
    chord (pads, 8, 32, 30, { 58, 62, 65 }, 0.48);

    // --- automation -----------------------------------------------------------
    // The Reese sweep: four bars of bandpass travel, and the reason a Reese is
    // a performance rather than a patch.
    const auto band = [] (double hz) { return curveValueIn ("filter", ids::cutoff, hz); };

    project.appendChild (makeAutomation (1, AutomationScope::channelEffect, 5, 0, ids::cutoff,
                                         { { 0.0, band (260.0) },
                                           { kBar * 1.0, band (1500.0), -0.4 },
                                           { kBar * 2.0, band (420.0), 0.35 },
                                           { kBar * 3.0, band (2400.0), -0.5 },
                                           { kBar * 4.0, band (300.0), 0.3 } }),
                         nullptr);

    // The break drops out of the second drop, which is a gain move on the whole
    // kit rather than on any one drum in it.
    project.appendChild (
        makeAutomation (
            2, AutomationScope::mixerTrack, 1, -1, ids::gain,
            { { 0.0, curveValue (AutomationScope::mixerTrack, ids::gain, 0.80) },
              { kBar * 4.0, curveValue (AutomationScope::mixerTrack, ids::gain, 0.10), 0.0,
                "step" },
              { kBar * 8.0, curveValue (AutomationScope::mixerTrack, ids::gain, 0.80), -0.4 } }),
        nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Break", "Bass", "Lead", "Pad", "Sweep", "Drop" });

    auto drums = laneAt (project, 0);
    drums.appendChild (makeClip (project, 1, 0, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 8, 16), nullptr);
    drums.appendChild (makeClip (project, 3, 24, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 32, 8), nullptr);
    drums.appendChild (makeClip (project, 3, 40, 16), nullptr);
    drums.appendChild (makeClip (project, 2, 56, 8), nullptr);

    auto bassLane = laneAt (project, 1);
    bassLane.appendChild (makeClip (project, 4, 8, 24), nullptr);
    bassLane.appendChild (makeClip (project, 4, 40, 20), nullptr);

    auto leadLane = laneAt (project, 2);
    leadLane.appendChild (makeClip (project, 5, 16, 8), nullptr);
    leadLane.appendChild (makeClip (project, 5, 44, 12), nullptr);

    auto padLane = laneAt (project, 3);
    padLane.appendChild (makeClip (project, 6, 0, 8), nullptr);
    padLane.appendChild (makeClip (project, 6, 32, 8), nullptr);
    padLane.appendChild (makeClip (project, 6, 56, 8), nullptr);

    auto sweepLane = laneAt (project, 4);
    sweepLane.appendChild (makeAutomationClip (project, 1, 8, 4), nullptr);
    sweepLane.appendChild (makeAutomationClip (project, 1, 16, 4), nullptr);
    sweepLane.appendChild (makeAutomationClip (project, 1, 40, 4), nullptr);
    sweepLane.appendChild (makeAutomationClip (project, 1, 48, 4), nullptr);

    laneAt (project, 5).appendChild (makeAutomationClip (project, 2, 24, 8), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
