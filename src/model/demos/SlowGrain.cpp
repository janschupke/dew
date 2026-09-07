// =============================================================================
// Slow Grain - half-time trap, 140 bpm, 4/4, C# Phrygian, 48 bars.
//
// Half-time means the snare falls on three and the hats run at four times the
// speed of everything else, so this project runs at EIGHT steps to a beat -
// thirty-two to a bar - because a thirty-second roll is not expressible at
// four and a roll is the genre's one indispensable figure.
//
// Two things here are the only ones of their kind in the library. The 808's
// pitch drop is a curve on detuneCents, and every automatable parameter but a
// wavetable position is LATCHED AT NOTE-ON - so the curve moves the NEXT note,
// not the one sounding. For a per-note pitch that is exactly the behaviour
// wanted, and it is worth one demo using the latch on purpose rather than every
// demo working around it. And the riser lands OFF the bar line, on the last
// eighth before the drop, which is what v20's finer clip grid bought and what
// nothing shipped had ever used.
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

constexpr int kBeat = 8;
constexpr int kBar = kBeat * 4;
constexpr int kCs1 = 25;

} // namespace

juce::ValueTree ProjectFactory::createSlowGrain()
{
    auto project = scaffold (7);

    setSong (project, "Slow Grain", 140.0, 48);
    setGrid (project, kBeat, 4, 4);

    const char* names[] { "808", "Kick", "Snare", "Hat", "Bell", "Pad", "Riser" };

    for (int i = 0; i < 7; ++i)
    {
        auto channel = channelWithId (project, i + 1);
        channel.setProperty (ids::name, names[i], nullptr);
        channel.setProperty (ids::colour, entityColour::defaultHex (i), nullptr);
    }

    // The 808: a sine long enough to be a bass note, with a second sine four
    // octaves up folded into it for the click. Fold distortion on its insert is
    // what makes it a distorted 808 rather than a sine.
    auto eight = channelWithId (project, 1);
    setClassicOsc (eight, 0, "sine", -1, 1.0);
    setClassicOsc (eight, 1, "sine", 3, 0.45);
    setFm (eight, 1, 0.12, 0.0, 0.0, 0.0);
    disableOsc (eight, 2);
    setAmp (eight, 0.001, 1.400, 0.42, 0.480);
    setMix (eight, 0.86, 0.0);

    auto kick = channelWithId (project, 2);
    setClassicOsc (kick, 0, "sine", -1, 0.85);
    setAmp (kick, 0.001, 0.090, 0.0, 0.040);
    setMix (kick, 0.62, 0.0);

    auto snare = channelWithId (project, 3);
    setClassicOsc (snare, 0, "square", 0, 0.46);
    setClassicOsc (snare, 1, "saw", 1, 0.44, 35);
    setAmp (snare, 0.001, 0.130, 0.0, 0.110);
    setMix (snare, 0.66, 0.0);

    auto hat = channelWithId (project, 4);
    setClassicOsc (hat, 0, "square", 2, 0.34, 25);
    setAmp (hat, 0.001, 0.022, 0.0, 0.020);
    setMix (hat, 0.26, 0.16);

    auto bell = channelWithId (project, 5);
    setClassicOsc (bell, 0, "triangle", 1, 0.60);
    setClassicOsc (bell, 1, "sine", 2, 0.34, 6);
    setAmp (bell, 0.002, 0.520, 0.10, 0.420);
    setMix (bell, 0.40, -0.20);

    auto pad = channelWithId (project, 6);
    setWavetableOsc (pad, 0, "pulse", 0.36, 0.30, "lfo", 0.09, 3, 13.0, 0, 0.42);
    setAmp (pad, 0.600, 1.500, 0.55, 1.300);
    setMix (pad, 0.30, 0.22);

    auto riser = channelWithId (project, 7);
    setWavetableOsc (riser, 0, "formant", 0.10, 0.90, "envelope", 0.5, 5, 26.0, 1, 0.55);
    setAmp (riser, 1.100, 0.900, 0.70, 0.400);
    setMix (riser, 0.34, 0.0);

    setInserts (project, { "808", "Drums", "Keys", "Air" });

    routeTo (eight, 1);

    for (int id = 2; id <= 4; ++id)
        routeTo (channelWithId (project, id), 2);

    routeTo (bell, 3);
    routeTo (pad, 3);
    routeTo (riser, 4);

    // The fold belongs to the 808, not to the desk: a distorted 808 is one
    // sound, and unplugging the channel from this insert should not turn it
    // back into a sine. The lowpass after it IS a desk decision, and stays one.
    eight.appendChild (makeDistortion (1, "fold", 2.4, 0.35, 0.58, 0.45), nullptr);

    mixerTrackWithId (project, 1).appendChild (makeFilter (2, "lowpass", 900.0, 0.9), nullptr);

    auto drumInsert = mixerTrackWithId (project, 2);
    drumInsert.appendChild (makeFilter (3, "highpass", 200.0, 0.7), nullptr);
    drumInsert.appendChild (makeEffect (4, "compressor",
                                        { { ids::threshold, -15.0 },
                                          { ids::ratio, 4.0 },
                                          { ids::attackMs, 5.0 },
                                          { ids::releaseMs, 100.0 },
                                          { ids::makeup, 2.5 } }),
                            nullptr);

    mixerTrackWithId (project, 3)
        .appendChild (
            makeEffect (5, "delay",
                        { { ids::delayMs, 428.0 }, { ids::feedback, 0.38 }, { ids::mix, 0.30 } }),
            nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (makeEffect (6, "reverb",
                                  { { ids::roomSize, 0.92 },
                                    { ids::damping, 0.32 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.60 } }),
                      nullptr);

    setMixerTrack (project, 1, 0.82, 0.0);
    setMixerTrack (project, 2, 0.74, 0.0);
    setMixerTrack (project, 3, 0.46, 0.0);
    setMixerTrack (project, 4, 0.36, 0.0);

    auto master = masterOf (project);
    master.appendChild (makeEffect (7, "eq",
                                    { { ids::lowGainDb, 3.0 },
                                      { ids::midGainDb, -2.0 },
                                      { ids::midFreq, 500.0 },
                                      { ids::highGainDb, 1.5 } }),
                        nullptr);
    master.appendChild (makeEffect (8, "limiter", { { ids::ceiling, -1.4 } }), nullptr);
    master.setProperty (ids::gain, stored (1.00), nullptr);

    // --- patterns -------------------------------------------------------------
    // Thirty-two characters to a bar: one per thirty-second, which is why the
    // rolls below can be written at all.
    auto beat = patternIn (project, 1, "Half-time", kBar);
    steps (beat, 2, 33, "x...............x.......x.......", 0.88, 4);
    steps (beat, 3, 62, "................x...............", 0.9, 6);
    steps (beat, 4, 92, "x...x...x...x...x...x...x...x...", 0.32, 2);

    auto rolls = patternIn (project, 2, "Rolls", kBar);
    steps (rolls, 2, 33, "x...............x.......x...x...", 0.88, 4);
    steps (rolls, 3, 62, "................x...............", 0.9, 6);
    steps (rolls, 4, 92, "x...x...x.x.xxxxx...x...xxxxxxxx", 0.32, 2);

    auto sparse = patternIn (project, 3, "Intro", kBar);
    steps (sparse, 4, 92, "x.......x.......x.......x.......", 0.28, 2);
    steps (sparse, 3, 62, "................x...............", 0.7, 6);

    // Four bars of C# - A - D - B roots: i, bVI, bII, bVII in Phrygian, which
    // is the mode's own cadence and the reason the flat second sits so high in
    // the mix.
    const int roots[4] { kCs1, kCs1 - 4, kCs1 + 1, kCs1 - 2 };

    auto eights = patternIn (project, 4, "808", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        notes (eights, 1,
               { { at, 20, roots[bar], 0.95 },
                 { at + 24, 6, roots[bar] + 12, 0.7 },
                 { at + kBeat * 2, 14, roots[bar], 0.82 } });
    }

    auto bells = patternIn (project, 5, "Bells", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * kBar;
        const auto top = roots[bar] + 48;
        notes (bells, 5,
               { { at, 6, top, 0.6 },
                 { at + 12, 4, top + 3, 0.5 },
                 { at + kBeat * 2, 6, top + 7, 0.58 },
                 { at + kBeat * 3 + 4, 8, top + 3, 0.46 } });
    }

    auto pads = patternIn (project, 6, "Pad", kBar * 4);

    for (int bar = 0; bar < 4; ++bar)
        chord (pads, 6, bar * kBar, kBar - 2, { roots[bar] + 24, roots[bar] + 27, roots[bar] + 31 },
               0.44);

    auto risers = patternIn (project, 7, "Riser", kBar);
    notes (risers, 7, { { 0, kBar, 61, 0.6 } });

    // --- automation -----------------------------------------------------------
    // The 808's drop. detuneCents is latched at note-on, so what this curve does
    // is set the pitch of each NEXT note - a bar of them a little flat, then
    // back. That is the behaviour, used rather than fought.
    const auto cents = [] (double value)
    { return curveValue (AutomationScope::channelOsc, ids::detuneCents, value); };

    project.appendChild (makeAutomation (1, AutomationScope::channelOsc, 1, 0, ids::detuneCents,
                                         { { 0.0, cents (0.0), 0.0, "step" },
                                           { kBar * 1.0, cents (-14.0), 0.0, "step" },
                                           { kBar * 2.0, cents (0.0), 0.0, "step" },
                                           { kBar * 3.0, cents (-24.0), 0.0, "step" } }),
                         nullptr);

    // The 808's tail grows through the drop and shortens for the outro. Latched
    // too, which is why it steps.
    const auto release = [] (double seconds)
    { return curveValue (AutomationScope::channelAmp, ids::release, seconds); };

    project.appendChild (makeAutomation (2, AutomationScope::channelAmp, 1, -1, ids::release,
                                         { { 0.0, release (0.30), 0.0, "step" },
                                           { kBar * 8.0, release (0.80), 0.0, "step" },
                                           { kBar * 20.0, release (0.24), 0.0, "step" } }),
                         nullptr);

    // --- arrangement ----------------------------------------------------------
    setLanes (project, { "Drums", "808", "Keys", "Riser", "Pitch", "Tail" });

    auto drums = laneAt (project, 0);
    drums.appendChild (makeClip (project, 3, 0, 8), nullptr);
    drums.appendChild (makeClip (project, 1, 8, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 16, 16), nullptr);
    drums.appendChild (makeClip (project, 1, 32, 8), nullptr);
    drums.appendChild (makeClip (project, 2, 40, 8), nullptr);

    auto eightLane = laneAt (project, 1);
    eightLane.appendChild (makeClip (project, 4, 8, 24), nullptr);
    eightLane.appendChild (makeClip (project, 4, 36, 12), nullptr);

    auto keysLane = laneAt (project, 2);
    keysLane.appendChild (makeClip (project, 6, 0, 16), nullptr);
    keysLane.appendChild (makeClip (project, 5, 16, 16), nullptr);
    keysLane.appendChild (makeClip (project, 6, 32, 16), nullptr);

    // Off the bar line: the riser starts on the last eighth of bar 15 so it
    // arrives WITH the drop rather than a beat before it. In bars that is not
    // sayable at all.
    auto riserLane = laneAt (project, 3);
    riserLane.appendChild (makeClipAtStep (7, 15 * kBar + kBeat * 3, kBeat), nullptr);
    riserLane.appendChild (makeClipAtStep (7, 39 * kBar + kBeat * 3, kBeat), nullptr);

    laneAt (project, 4).appendChild (makeAutomationClip (project, 1, 16, 16), nullptr);
    laneAt (project, 5).appendChild (makeAutomationClip (project, 2, 8, 24), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
