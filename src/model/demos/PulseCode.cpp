// =============================================================================
// Pulse Code - techno, 132 bpm, 4/4, A Phrygian, 64 bars.
//
// The arrangement a techno track actually has: eight bars of kick, eight that
// build, sixteen that land, a break, sixteen more, and eight to leave on. The
// harmony is the flat second doing all the work - Am to Bb and back - which is
// what Phrygian is for and what four-on-the-floor leaves room for.
//
// This is the demo that owns the MIXER. Four drum channels share one insert,
// because gluing a kit with one drive and one compressor is the thing a bus is
// for and no shipped project had ever done it; the pumping is a curve on that
// insert's neighbours' gain rather than an effect, because that is what
// sidechain compression is imitating and dew has no sidechain input.
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

// A Phrygian: A Bb C D E F G. The roots the whole track turns on, an octave
// apart in the bass and in the stabs.
constexpr int kA1 = 33;
constexpr int kBb1 = 34;
constexpr int kF1 = 29;
constexpr int kG1 = 31;

constexpr int kBar = 16;

/** One bar of the kit, at whatever intensity this section is playing. */
void kit (juce::ValueTree pattern, bool clap, juce::StringRef hats, double level)
{
    steps (pattern, 1, kA1, "x...x...x...x...", level, 6);

    if (clap)
        steps (pattern, 2, 60, "....x.......x...", level * 0.9, 4);

    steps (pattern, 3, 84, hats, level * 0.5, 1);
}

} // namespace

juce::ValueTree ProjectFactory::createPulseCode()
{
    auto project = scaffold (8);

    setSong (project, "Pulse Code", 132.0, 64);
    setGrid (project, 4, 4, 4);

    const char* names[] { "Kick", "Clap", "Hat", "Rim", "Bass", "Stab", "Lead", "Air" };

    for (int i = 0; i < 8; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    // --- the kit --------------------------------------------------------------
    auto kick = channelWithId (project, 1);
    setClassicOsc (kick, 0, "sine", -1, 1.0);
    setAmp (kick, 0.001, 0.170, 0.0, 0.050);
    setMix (kick, 0.95, 0.0);

    auto clap = channelWithId (project, 2);
    setClassicOsc (clap, 0, "square", 0, 0.5);
    setClassicOsc (clap, 1, "saw", 1, 0.35, 37);
    setAmp (clap, 0.001, 0.110, 0.0, 0.090);
    setMix (clap, 0.55, -0.12);

    auto hat = channelWithId (project, 3);
    setClassicOsc (hat, 0, "square", 1, 0.4);
    setClassicOsc (hat, 1, "square", 2, 0.3, 43);
    setAmp (hat, 0.001, 0.035, 0.0, 0.030);
    setMix (hat, 0.40, 0.18);

    auto rim = channelWithId (project, 4);
    setClassicOsc (rim, 0, "triangle", 0, 0.6);
    setAmp (rim, 0.001, 0.050, 0.0, 0.040);
    setMix (rim, 0.45, -0.24);

    // --- the voices -----------------------------------------------------------
    auto bass = channelWithId (project, 5);
    setClassicOsc (bass, 0, "saw", 0, 0.7);
    setClassicOsc (bass, 1, "sine", -1, 0.6);
    setAmp (bass, 0.003, 0.140, 0.55, 0.070);
    setMix (bass, 0.80, 0.0);

    auto stab = channelWithId (project, 6);
    setClassicOsc (stab, 0, "saw", 0, 0.55, -8);
    setClassicOsc (stab, 1, "saw", 0, 0.55, 9);
    setClassicOsc (stab, 2, "square", -1, 0.30);
    setAmp (stab, 0.004, 0.220, 0.18, 0.140);
    setMix (stab, 0.50, 0.10);

    auto lead = channelWithId (project, 7);
    setWavetableOsc (lead, 0, "fold", 0.30, 0.45, "envelope", 0.5, 3, 9.0, 0, 0.7);
    setAmp (lead, 0.006, 0.260, 0.35, 0.220);
    setMix (lead, 0.42, -0.08);

    auto air = channelWithId (project, 8);
    setWavetableOsc (air, 0, "pulse", 0.15, 0.85, "lfo", 0.22, 5, 22.0, 1, 0.5);
    setAmp (air, 0.900, 1.200, 0.60, 1.400);
    setMix (air, 0.30, 0.0);

    // --- routing --------------------------------------------------------------
    // Five inserts for eight channels: the kit is one fader, which is the point.
    setInserts (project, { "Drums", "Bass", "Stab", "Lead", "Air" });

    for (int id = 1; id <= 4; ++id)
        routeTo (channelWithId (project, id), 1);

    routeTo (bass, 2);
    routeTo (stab, 3);
    routeTo (lead, 4);
    routeTo (air, 5);

    auto drums = mixerTrackWithId (project, 1);
    drums.appendChild (makeEffect (1, "drive", { { ids::drive, 3.4 }, { ids::outputGain, 0.72 } }),
                       nullptr);
    drums.appendChild (makeEffect (2, "compressor",
                                   { { ids::threshold, -16.0 },
                                     { ids::ratio, 4.5 },
                                     { ids::attackMs, 4.0 },
                                     { ids::releaseMs, 120.0 },
                                     { ids::makeup, 3.0 } }),
                       nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (3, "eq",
                                  { { ids::lowGainDb, 3.0 },
                                    { ids::midGainDb, -2.5 },
                                    { ids::midFreq, 520.0 },
                                    { ids::highGainDb, -4.0 } }),
                      nullptr);

    mixerTrackWithId (project, 3)
        .appendChild (
            makeEffect (4, "delay",
                        { { ids::delayMs, 341.0 }, { ids::feedback, 0.34 }, { ids::mix, 0.28 } }),
            nullptr);

    auto leadInsert = mixerTrackWithId (project, 4);
    leadInsert.appendChild (makeFilter (5, "lowpass", 2600.0, 1.5), nullptr);
    leadInsert.appendChild (makeDistortion (9, "softClip", 2.6, 0.62, 0.70), nullptr);

    // Highpass first, then the reverb: sending a kick's low end into a room the
    // size of this one is what makes a techno mix muddy, and the order is the
    // whole reason a chain is a chain.
    auto airInsert = mixerTrackWithId (project, 5);
    airInsert.appendChild (makeFilter (10, "highpass", 420.0, 0.7), nullptr);
    airInsert.appendChild (makeEffect (6, "reverb",
                                       { { ids::roomSize, 0.82 },
                                         { ids::damping, 0.35 },
                                         { ids::width, 1.0 },
                                         { ids::mix, 0.45 } }),
                           nullptr);

    setMixerTrack (project, 1, 0.82, 0.0);
    setMixerTrack (project, 2, 0.74, 0.0);
    setMixerTrack (project, 3, 0.58, 0.06);
    setMixerTrack (project, 4, 0.52, -0.06);
    setMixerTrack (project, 5, 0.34, 0.0);

    auto master = masterOf (project);
    master.appendChild (makeEffect (7, "eq",
                                    { { ids::lowGainDb, 1.5 },
                                      { ids::midGainDb, -1.0 },
                                      { ids::midFreq, 900.0 },
                                      { ids::highGainDb, 2.0 } }),
                        nullptr);
    master.appendChild (makeEffect (8, "limiter", { { ids::ceiling, -1.2 } }), nullptr);
    master.setProperty (ids::gain, stored (1.00), nullptr);

    // --- patterns -------------------------------------------------------------
    auto intro = patternIn (project, 1, "Intro", kBar);
    kit (intro, false, "..x...x...x...x.", 0.85);

    auto build = patternIn (project, 2, "Build", kBar);
    kit (build, true, "..x...x...x..xx.", 0.9);
    steps (build, 4, 72, "......x.......x.", 0.5, 3);

    auto drop = patternIn (project, 3, "Drop", kBar);
    kit (drop, true, "..x.o.x.o.x.o.x.", 1.0);
    steps (drop, 4, 72, "......x...x...x.", 0.55, 3);

    auto brk = patternIn (project, 4, "Break", kBar);
    steps (brk, 2, 60, "....x.......x...", 0.75, 4);
    steps (brk, 3, 84, "..x...x...x...x.", 0.35, 1);
    steps (brk, 4, 72, "x...x...x...x...", 0.5, 3);

    // Two bars, so the root moves under a one-bar kick without the kit having
    // to know. A clip four bars long repeats it twice, which is the thing a
    // clip longer than its pattern is for.
    auto bassline = patternIn (project, 5, "Bassline", kBar * 2);
    notes (bassline, 5,
           { { 0, 3, kA1, 0.92 },
             { 6, 3, kA1, 0.7 },
             { 10, 3, kA1, 0.8 },
             { 14, 2, kBb1, 0.72 },
             { 16, 3, kF1, 0.9 },
             { 22, 3, kF1, 0.68 },
             { 26, 3, kG1, 0.82 },
             { 30, 2, kG1 + 2, 0.7 } });

    // Four bars of Am - Bb - F - Gm, voiced tight so the flat second is the
    // only thing that moves.
    auto stabs = patternIn (project, 6, "Stabs", kBar * 4);
    chord (stabs, 6, 2, 6, { 57, 60, 64 }, 0.62);
    chord (stabs, 6, 10, 4, { 57, 60, 64 }, 0.5);
    chord (stabs, 6, 18, 6, { 58, 62, 65 }, 0.66);
    chord (stabs, 6, 26, 4, { 58, 62, 65 }, 0.52);
    chord (stabs, 6, 34, 6, { 57, 60, 65 }, 0.6);
    chord (stabs, 6, 42, 4, { 57, 60, 65 }, 0.48);
    chord (stabs, 6, 50, 6, { 58, 62, 67 }, 0.64);
    chord (stabs, 6, 58, 4, { 58, 62, 67 }, 0.5);

    auto leadline = patternIn (project, 7, "Lead", kBar * 4);
    notes (leadline, 7,
           { { 0, 6, 69, 0.7 },
             { 8, 4, 72, 0.6 },
             { 14, 6, 70, 0.68 },
             { 24, 8, 69, 0.72 },
             { 34, 4, 76, 0.75 },
             { 40, 4, 74, 0.6 },
             { 46, 10, 72, 0.7 },
             { 58, 6, 69, 0.62 } });

    auto sweep = patternIn (project, 8, "Air", kBar * 8);
    notes (sweep, 8, { { 0, 64, 57, 0.5 }, { 64, 64, 58, 0.55 } });

    // --- automation -----------------------------------------------------------
    // Every value below is a REAL one - a gain, a frequency - put through the
    // parameter's own range by curveValue. A point stores the normalised form,
    // and writing that form by hand is how a curve comes to mean something
    // nobody can read off the source.

    // The pump: ducked on each of the eight beats of two bars, a STEP down and
    // a bent recovery. The duck is an event and the recovery is a motion, which
    // is what a compressor released against a kick actually does.
    const auto ducked = curveValue (AutomationScope::mixerTrack, ids::gain, 0.24);
    const auto open = curveValue (AutomationScope::mixerTrack, ids::gain, 0.74);

    std::vector<Point> pump;

    for (int beat = 0; beat < 8; ++beat)
    {
        pump.push_back ({ beat * 4.0, ducked, 0.0, "step" });
        pump.push_back ({ beat * 4.0 + 3.0, open, -0.45 });
    }

    project.appendChild (
        makeAutomation (1, AutomationScope::mixerTrack, 2, -1, ids::gain, std::move (pump)),
        nullptr);

    // The riser over the build: 240 Hz to 9 kHz across eight bars, bent so most
    // of the travel is in the last two rather than spread evenly.
    project.appendChild (
        makeAutomation (2, AutomationScope::mixerEffect, 4, 0, ids::cutoff,
                        { { 0.0, curveValueIn ("filter", ids::cutoff, 240.0) },
                          { 128.0, curveValueIn ("filter", ids::cutoff, 9000.0), -0.55 } }),
        nullptr);

    project.appendChild (
        makeAutomation (3, AutomationScope::master, 0, -1, ids::gain,
                        { { 0.0, curveValue (AutomationScope::master, ids::gain, 1.00) },
                          { 96.0, curveValue (AutomationScope::master, ids::gain, 1.00) },
                          { 128.0, 0.0, 0.35 } }),
        nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Drums", "Bass", "Keys", "Air", "Pump", "Sweep", "Fade" });

    auto drumLane = laneAt (project, 0);
    drumLane.appendChild (makeClip (project, 1, 0, 8), nullptr);
    drumLane.appendChild (makeClip (project, 2, 8, 8), nullptr);
    drumLane.appendChild (makeClip (project, 3, 16, 16), nullptr);
    drumLane.appendChild (makeClip (project, 4, 32, 8), nullptr);
    drumLane.appendChild (makeClip (project, 3, 40, 16), nullptr);
    drumLane.appendChild (makeClip (project, 2, 56, 8), nullptr);

    auto bassLane = laneAt (project, 1);
    bassLane.appendChild (makeClip (project, 5, 8, 24), nullptr);
    bassLane.appendChild (makeClip (project, 5, 40, 16), nullptr);

    auto keysLane = laneAt (project, 2);
    keysLane.appendChild (makeClip (project, 6, 16, 16), nullptr);
    keysLane.appendChild (makeClip (project, 7, 40, 16), nullptr);
    keysLane.appendChild (makeClip (project, 6, 56, 8), nullptr);

    auto airLane = laneAt (project, 3);
    airLane.appendChild (makeClip (project, 8, 24, 8), nullptr);
    airLane.appendChild (makeClip (project, 8, 32, 8), nullptr);

    auto pumpLane = laneAt (project, 4);

    for (int bar = 16; bar < 32; bar += 2)
        pumpLane.appendChild (makeAutomationClip (project, 1, bar, 2), nullptr);

    for (int bar = 40; bar < 56; bar += 2)
        pumpLane.appendChild (makeAutomationClip (project, 1, bar, 2), nullptr);

    laneAt (project, 5).appendChild (makeAutomationClip (project, 2, 8, 8), nullptr);
    laneAt (project, 6).appendChild (makeAutomationClip (project, 3, 56, 8), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
