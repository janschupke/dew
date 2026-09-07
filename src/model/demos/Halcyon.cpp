// =============================================================================
// Halcyon - ambient, 6/8, D Lydian, 48 bars. Notes from examples/halcyon.score.
//
// The language owns the music and this file owns the instrument. That division
// is the score language's own, and it is what lets an ambient piece be written
// as harmony and counterpoint while the thing a listener actually notices - a
// wavetable moving under a held chord - is dialled in beside it in C++.
//
// Two pads, morphing from different places on purpose. The held one takes its
// position from a free-running LFO, so it never lines up with the bar and never
// repeats; the moving one takes it from the ENVELOPE, so every note starts at
// the same timbre and travels. A pad wants the first and a struck sound wants
// the second, and hearing them next to each other is the only way that is
// obvious.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"
#include "model/demos/ScoreDemo.h"

namespace dew
{

using namespace demo;

juce::ValueTree ProjectFactory::createHalcyon()
{
    auto project = compiledScore ("halcyon.score");

    if (! project.isValid())
        return createDefault();

    auto pad = channelNamed (project, "pad");
    auto veil = channelNamed (project, "veil");
    auto sub = channelNamed (project, "sub");
    auto bell = channelNamed (project, "bell");
    auto answer = channelNamed (project, "answer");

    // The bed. Formant frames, five voices of unison spread wide enough to
    // beat slowly at this tempo, and a position that runs free.
    setWavetableOsc (pad, 0, "formant", 0.22, 0.55, "lfo", 0.07, 5, 16.0, 0, 0.62);
    setClassicOsc (pad, 1, "triangle", -1, 0.28, -6);
    setAmp (pad, 1.400, 2.600, 0.72, 2.800);
    setMix (pad, 0.58, -0.12);
    setLfo (pad, 1, "sine", 0.09, 0.0, 0.0, 0.85);

    // The moving one. Harmonics frames under the ENVELOPE, so the timbre opens
    // across a note rather than wandering across a bar.
    setWavetableOsc (veil, 0, "harmonics", 0.10, 0.80, "envelope", 0.5, 3, 11.0, 0, 0.55);
    setAmp (veil, 0.900, 2.200, 0.55, 2.400);
    setMix (veil, 0.44, 0.16);

    setClassicOsc (sub, 0, "sine", -1, 0.90);
    setClassicOsc (sub, 1, "triangle", 0, 0.24);
    setAmp (sub, 0.080, 1.400, 0.70, 0.900);
    setMix (sub, 0.72, 0.0);

    // The bell, and the reason this demo has an FM matrix at all: a sine two
    // octaves up folded into a sine, at an amount low enough to stay a bell
    // rather than become noise. Slot 2 is heard by nothing - fmOut 0 - which is
    // what a pure modulator is.
    setClassicOsc (bell, 0, "sine", 0, 0.72);
    setClassicOsc (bell, 1, "sine", 2, 0.60);
    setFm (bell, 1, 0.26, 0.0, 0.0, 0.0);
    disableOsc (bell, 2);
    setAmp (bell, 0.004, 1.100, 0.10, 1.600);
    setMix (bell, 0.40, 0.28);

    setClassicOsc (answer, 0, "triangle", 0, 0.62);
    setClassicOsc (answer, 1, "sine", 1, 0.22, 8);
    setAmp (answer, 0.220, 1.200, 0.48, 1.100);
    setMix (answer, 0.34, -0.30);

    // --- the room -------------------------------------------------------------
    mixerTrackWithId (project, 1).appendChild (makeFilter (1, "lowpass", 3400.0, 0.6), nullptr);
    mixerTrackWithId (project, 1)
        .appendChild (makeEffect (2, "chorus",
                                  { { ids::rate, 0.18 }, { ids::depth, 0.55 }, { ids::mix, 0.5 } }),
                      nullptr);

    // mix 0.62, not 1.0: an ambient reverb that replaces its input is a wash,
    // and the whole point of this pad is that something dry is still moving
    // inside it. No shipped project had ever set an effect's mix at all.
    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (3, "reverb",
                                  { { ids::roomSize, 0.94 },
                                    { ids::damping, 0.28 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.62 } }),
                      nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (
            makeEffect (4, "delay",
                        { { ids::delayMs, 577.0 }, { ids::feedback, 0.48 }, { ids::mix, 0.38 } }),
            nullptr);

    mixerTrackWithId (project, 5)
        .appendChild (makeEffect (5, "chorus",
                                  { { ids::rate, 0.42 }, { ids::depth, 0.4 }, { ids::mix, 0.45 } }),
                      nullptr);

    setMixerTrack (project, 1, 0.72, 0.0);
    setMixerTrack (project, 2, 0.58, 0.10);
    setMixerTrack (project, 3, 0.66, 0.0);
    setMixerTrack (project, 4, 0.46, 0.18);
    setMixerTrack (project, 5, 0.40, -0.20);

    auto master = masterOf (project);
    master.appendChild (makeEffect (6, "reverb",
                                    { { ids::roomSize, 0.86 },
                                      { ids::damping, 0.42 },
                                      { ids::width, 1.0 },
                                      { ids::mix, 0.30 } }),
                        nullptr);
    master.appendChild (makeEffect (7, "eq",
                                    { { ids::lowGainDb, -1.0 },
                                      { ids::midGainDb, 0.5 },
                                      { ids::midFreq, 1400.0 },
                                      { ids::highGainDb, 1.5 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (0.60), nullptr);

    // --- the curves -----------------------------------------------------------
    const auto bar = stepsPerBar (project);
    const auto padId = (int) pad[ids::id];
    const auto veilId = (int) veil[ids::id];

    // The pad's morph across the whole piece: out and back, bent so it lingers
    // at both ends rather than passing through them.
    project.appendChild (makeAutomation (1, AutomationScope::channelOsc, padId, 0,
                                         ids::wavePosition,
                                         { { 0.0, 0.18 },
                                           { bar * 16.0, 0.74, -0.45 },
                                           { bar * 32.0, 0.30, 0.35 },
                                           { bar * 48.0, 0.62, -0.25 } }),
                         nullptr);

    // The veil's release lengthens as the piece opens. Every automatable
    // parameter but a wavetable position is LATCHED AT NOTE-ON, so this moves
    // the next note rather than the one sounding - which for a release is
    // exactly right, and is why the curve steps between sections rather than
    // sliding through them.
    project.appendChild (
        makeAutomation (
            2, AutomationScope::channelAmp, veilId, -1, ids::release,
            { { 0.0, curveValue (AutomationScope::channelAmp, ids::release, 1.2), 0.0, "step" },
              { bar * 8.0, curveValue (AutomationScope::channelAmp, ids::release, 2.4), 0.0,
                "step" },
              { bar * 24.0, curveValue (AutomationScope::channelAmp, ids::release, 4.0), 0.0,
                "step" },
              { bar * 40.0, curveValue (AutomationScope::channelAmp, ids::release, 2.0), 0.0,
                "step" } }),
        nullptr);

    project.appendChild (
        makeAutomation (3, AutomationScope::mixerEffect, 2, 0, ids::mix,
                        { { 0.0, 0.35 }, { bar * 24.0, 0.78, -0.30 }, { bar * 48.0, 0.40, 0.25 } }),
        nullptr);

    setLanes (project, { "Score", "Pad morph", "Veil tail", "Room" });

    laneAt (project, 1).appendChild (makeAutomationClip (project, 1, 0, 48), nullptr);
    laneAt (project, 2).appendChild (makeAutomationClip (project, 2, 0, 48), nullptr);
    laneAt (project, 3).appendChild (makeAutomationClip (project, 3, 0, 48), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
