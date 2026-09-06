// =============================================================================
// The effect chain's demo, and automation's.
//
// One of four translation units holding the demo library's CONTENT. The
// structure - the document File > New produces, and the table naming what
// ships - is ProjectFactory.cpp; the vocabulary these are written in is
// DemoBuilders.h; the loader that reads the committed .dew files rather than
// re-running any of this is DemoLibrary.h.
//
// Together because they are the two that exist to show a PARAMETER being
// changed - one by hand down a chain, one by a curve over time.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/ProjectEdits.h"
#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

juce::ValueTree ProjectFactory::createEffectsDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Effect Chain", nullptr);
    project.setProperty (ids::tempoBpm, 110.0, nullptr);
    project.setProperty (ids::barsInSong, 32, nullptr);

    auto pad = channelWithId (project, 1);
    auto pluck = channelWithId (project, 2);
    auto bass = channelWithId (project, 3);
    auto kick = channelWithId (project, 4);

    pad.setProperty (ids::name, "Pad", nullptr);
    pad.setProperty (ids::volume, 0.26, nullptr);
    setClassicOsc (pad, 0, "saw", 0, 0.8);
    setAmp (pad, 0.350, 0.500, 0.850, 0.900);

    pluck.setProperty (ids::name, "Pluck", nullptr);
    pluck.setProperty (ids::volume, 0.32, nullptr);
    setClassicOsc (pluck, 0, "triangle", 0, 0.8);
    setAmp (pluck, 0.001, 0.220, 0.000, 0.100);

    bass.setProperty (ids::name, "Bass", nullptr);
    bass.setProperty (ids::volume, 0.42, nullptr);
    setAmp (bass, 0.004, 0.240, 0.400, 0.120);

    kick.setProperty (ids::name, "Kick", nullptr);
    kick.setProperty (ids::basePitch, 36, nullptr);
    kick.setProperty (ids::volume, 0.55, nullptr);
    setClassicOsc (kick, 0, "sine", 0, 0.8);
    setAmp (kick, 0.001, 0.160, 0.000, 0.060);

    // A chain four deep - the longest in the library, which shipped nothing
    // past two before it. Four is a musical choice, not the schema's limit;
    // a chain may now be nine. Order is the point: the filter shapes the saw, the drive works on
    // what is left, the chorus widens THAT, and the delay repeats the finished sound. Put the drive
    // last and it distorts the delay's tail instead.
    pad.appendChild (makeEffect (1, "filter", { { ids::cutoff, 1400.0 }, { ids::resonance, 1.6 } }),
                     nullptr);
    pad.appendChild (
        makeEffect (2, "drive",
                    { { ids::drive, 3.5 }, { ids::outputGain, 0.75 }, { ids::mix, 0.55 } }),
        nullptr);
    pad.appendChild (
        makeEffect (3, "chorus", { { ids::rate, 0.6 }, { ids::depth, 0.45 }, { ids::mix, 0.60 } }),
        nullptr);
    pad.appendChild (
        makeEffect (4, "delay",
                    { { ids::delayMs, 545.0 }, { ids::feedback, 0.30 }, { ids::mix, 0.22 } }),
        nullptr);

    // BANDPASS, and on the channel whose sound is nothing but transient: every
    // filterMode in every file the library shipped was lowpass, so two thirds
    // of the control had never been heard.
    auto banded = makeEffect (5, "filter", { { ids::cutoff, 1800.0 }, { ids::resonance, 2.4 } });
    banded.setProperty (ids::filterMode, "bandpass", nullptr);
    pluck.appendChild (banded, nullptr);

    // Bypassed, and left in deliberately. A slot that is switched off rather
    // than removed is how anyone actually works - it is an A/B, not a decision
    // - and the engine skips a chain with nothing enabled bit-exactly, which is
    // a path no shipped project exercised.
    auto bypassed = makeEffect (6, "eq", { { ids::lowGainDb, -6.0 }, { ids::highGainDb, 4.5 } });
    bypassed.setProperty (ids::enabled, false, nullptr);
    pluck.appendChild (bypassed, nullptr);

    bass.appendChild (makeEffect (7, "drive", { { ids::drive, 6.0 }, { ids::outputGain, 0.9 } }),
                      nullptr);

    // HIGHPASS, to keep the bass out of the kick's octave. The third mode, and
    // the reason a filter is not just a tone control.
    auto rumble = makeEffect (8, "filter", { { ids::cutoff, 55.0 }, { ids::resonance, 0.5 } });
    rumble.setProperty (ids::filterMode, "highpass", nullptr);
    bass.appendChild (rumble, nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);
    int trackIndex = 0;

    for (auto track : mixer)
    {
        if (! track.hasType (ids::MIXER_TRACK))
            continue;

        // Four channels through reverb, delay and drive stack up fast; the
        // first version of this demo peaked at 1.77 and clipped every bar.
        track.setProperty (ids::gain, 0.55, nullptr);

        // Insert 2 carries the delay the pluck feeds; insert 1 gets the reverb
        // the pad sits in, so both places a chain can live are demonstrated.
        if (trackIndex == 0)
            track.appendChild (makeEffect (9, "reverb",
                                           { { ids::roomSize, 0.85 },
                                             { ids::damping, 0.25 },
                                             { ids::width, 0.65 },
                                             { ids::mix, 0.4 } }),
                               nullptr);

        if (trackIndex == 1)
            track.appendChild (
                makeEffect (
                    10, "delay",
                    { { ids::delayMs, 340.0 }, { ids::feedback, 0.45 }, { ids::mix, 0.35 } }),
                nullptr);

        if (trackIndex == 2)
            track.appendChild (makeEffect (11, "eq",
                                           { { ids::lowGainDb, 5.0 },
                                             { ids::midGainDb, -4.0 },
                                             { ids::midFreq, 700.0 },
                                             { ids::highGainDb, -3.0 } }),
                               nullptr);

        ++trackIndex;
    }

    // The third place a chain can live, and the one no demo had ever used.
    // EQ before reverb: shaping what goes INTO a tank is a different sound from
    // shaping what comes out, and this order is the one that keeps the low end
    // out of the tail rather than trying to take it out afterwards.
    if (auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        master.setProperty (ids::gain, 0.85, nullptr);
        master.appendChild (makeEffect (12, "eq",
                                        { { ids::lowGainDb, -2.5 },
                                          { ids::midGainDb, 1.5 },
                                          { ids::midFreq, 2200.0 },
                                          { ids::highGainDb, 2.0 } }),
                            nullptr);
        master.appendChild (makeEffect (13, "reverb",
                                        { { ids::roomSize, 0.55 },
                                          { ids::damping, 0.45 },
                                          { ids::width, 1.0 },
                                          { ids::mix, 0.18 } }),
                            nullptr);
    }

    // --- the music ------------------------------------------------------------
    // Four bars a pattern, so a clip is a section rather than a loop, and the
    // arrangement below is what turns four patterns into thirty-two bars that
    // go somewhere.
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 52, 55, 60 }, { 50, 55, 59 } };
    const int roots[4] = { 45, 41, 48, 43 };

    auto intro = patternIn (project, 1, "Intro", 64);
    auto groove = patternIn (project, 2, "Groove", 64);
    auto swellP = patternIn (project, 3, "Swell", 64);
    auto outro = patternIn (project, 4, "Outro", 64);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * 16;

        // The pad holds through every pattern: it is what the long chains are
        // working on, and a demo about a reverb tail needs something to feed it.
        for (const auto pitch : chords[bar])
        {
            intro.appendChild (makeNote (1, at, 15, pitch, 0.45), nullptr);
            groove.appendChild (makeNote (1, at, 15, pitch, 0.55), nullptr);
            swellP.appendChild (makeNote (1, at, 15, pitch + 12, 0.40), nullptr);
            outro.appendChild (makeNote (1, at, 15, pitch, 0.40), nullptr);
        }

        for (int step = at; step < at + 16; step += 4)
        {
            groove.appendChild (makeNote (4, step, 1, 36, 1.0), nullptr);
            intro.appendChild (makeNote (4, step, 1, 36, 0.7), nullptr);
        }

        for (int i = 0; i < 4; ++i)
            groove.appendChild (makeNote (3, at + i * 4 + 2, 2, roots[bar], 0.8), nullptr);

        // Plucks on the offbeats, which is where a delay is audible AS a delay.
        for (int i = 0; i < 8; ++i)
        {
            const auto pitch = chords[bar][(size_t) (i % 3)] + 12;
            groove.appendChild (makeNote (2, at + i * 2 + 1, 1, pitch, 0.65), nullptr);
            swellP.appendChild (makeNote (2, at + i * 2 + 1, 1, pitch, 0.5), nullptr);
        }
    }

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto lane = playlist.getChild (0);
    lane.setProperty (ids::name, "Arrangement", nullptr);

    lane.appendChild (makeClip (1, 0, 4), nullptr);
    lane.appendChild (makeClip (2, 4, 8), nullptr);
    lane.appendChild (makeClip (3, 12, 4), nullptr);
    lane.appendChild (makeClip (2, 16, 8), nullptr);
    lane.appendChild (makeClip (3, 24, 4), nullptr);
    lane.appendChild (makeClip (4, 28, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createAutomationDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Automation", nullptr);
    project.setProperty (ids::tempoBpm, 126.0, nullptr);
    project.setProperty (ids::barsInSong, 32, nullptr);

    auto arp = channelWithId (project, 1);
    auto kick = channelWithId (project, 2);
    auto bass = channelWithId (project, 3);
    auto stab = channelWithId (project, 4);

    arp.setProperty (ids::name, "Arp", nullptr);
    arp.setProperty (ids::volume, 0.50, nullptr);
    setClassicOsc (arp, 0, "saw", 0, 0.8);
    setAmp (arp, 0.002, 0.160, 0.250, 0.120);

    kick.setProperty (ids::name, "Kick", nullptr);
    kick.setProperty (ids::basePitch, 36, nullptr);
    kick.setProperty (ids::volume, 0.90, nullptr);
    setClassicOsc (kick, 0, "sine", 0, 0.8);
    setAmp (kick, 0.001, 0.150, 0.000, 0.060);

    bass.setProperty (ids::name, "Bass", nullptr);
    bass.setProperty (ids::volume, 0.55, nullptr);
    setClassicOsc (bass, 0, "saw", -1, 0.8);
    setAmp (bass, 0.003, 0.200, 0.550, 0.100);

    stab.setProperty (ids::name, "Stab", nullptr);
    stab.setProperty (ids::volume, 0.30, nullptr);
    setClassicOsc (stab, 0, "square", 0, 0.8);
    setAmp (stab, 0.004, 0.180, 0.300, 0.260);

    // The filter this demo sweeps. Wide open to start with, so the sweep has
    // somewhere to travel from.
    arp.appendChild (
        makeEffect (1, "filter", { { ids::cutoff, 16000.0 }, { ids::resonance, 2.2 } }), nullptr);
    stab.appendChild (
        makeEffect (2, "chorus", { { ids::rate, 0.9 }, { ids::depth, 0.25 }, { ids::mix, 0.5 } }),
        nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);

    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            track.setProperty (ids::gain, 0.85, nullptr);

    // The delay whose FEEDBACK is automated below. On an insert rather than the
    // channel deliberately: mixerEffect was the one scope in the enum that no
    // shipped project pointed a curve at.
    if (auto stabTrack = ProjectEdits::findMixerTrack (project, 4); stabTrack.isValid())
        stabTrack.appendChild (
            makeEffect (3, "delay",
                        { { ids::delayMs, 357.0 }, { ids::feedback, 0.20 }, { ids::mix, 0.30 } }),
            nullptr);

    // --- the music ------------------------------------------------------------
    const int arpNotes[16] = { 57, 60, 64, 69, 64, 60, 64, 69, 57, 60, 64, 72, 69, 64, 60, 64 };

    auto main = patternIn (project, 1, "Main", 64);
    auto brk = patternIn (project, 2, "Break", 64);

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * 16;
        const auto lift = bar >= 2 ? 3 : 0;

        for (int i = 0; i < 16; ++i)
        {
            main.appendChild (makeNote (1, at + i, 1, arpNotes[i] + lift, i % 4 == 0 ? 0.9 : 0.6),
                              nullptr);
            brk.appendChild (
                makeNote (1, at + i, 1, arpNotes[i] + lift + 12, i % 4 == 0 ? 0.7 : 0.4), nullptr);
        }

        for (int step = at; step < at + 16; step += 4)
            main.appendChild (makeNote (2, step, 1, 36, 1.0), nullptr);

        for (int step = at; step < at + 16; step += 8)
            main.appendChild (makeNote (3, step, 6, 33 + lift, 0.85), nullptr);

        // The break drops the kick and the bass and leaves the stabs, which is
        // what makes the feedback climbing under it audible as a climb.
        for (const auto pitch : { 69, 72, 76 })
            brk.appendChild (makeNote (4, at + 4, 8, pitch + lift, 0.55), nullptr);
    }

    // --- the curves -----------------------------------------------------------
    // Sixteen steps to a bar. Every one of these says something the library did
    // not: a bend that is not zero, a segment that steps rather than slides,
    // three scopes nothing had aimed at, and a parameter that is not `cutoff`.

    // Bent, not straight. A positive bend holds the low end of the sweep longer,
    // which is what makes a filter opening sound like it accelerates - a
    // straight line through the same two points arrives too early and sits.
    project.appendChild (
        makeAutomation (1, "Arp > Filter > Cutoff", AutomationScope::channelEffect, 1, 0,
                        ids::cutoff,
                        { { 0.0, 0.35, 0.55 }, { 96.0, 0.62, 0.30 }, { 192.0, 1.00 } }),
        nullptr);

    // 0.6, not 0.9: a point's value is normalised into the target's range, and
    // the master fader spans 0..1.5.
    project.appendChild (makeAutomation (2, "Master > Gain", AutomationScope::master, 0, -1,
                                         ids::gain,
                                         { { 0.0, 0.60 }, { 32.0, 0.60 }, { 64.0, 0.0 } }),
                         nullptr);

    // The pump. Ducked on the beat and recovering across it, four bars of it,
    // and the DUCK is a step while the recovery is a curve - because the duck
    // is an event and the recovery is a motion.
    project.appendChild (makeAutomation (3, "Bass > Volume", AutomationScope::channel, 3, -1,
                                         ids::volume,
                                         { { 0.0, 0.32, 0.0, "step" },
                                           { 4.0, 0.90, -0.45 },
                                           { 16.0, 0.32, 0.0, "step" },
                                           { 20.0, 0.90, -0.45 },
                                           { 32.0, 0.32, 0.0, "step" },
                                           { 36.0, 0.90, -0.45 },
                                           { 48.0, 0.32, 0.0, "step" },
                                           { 52.0, 0.90, -0.45 },
                                           { 64.0, 0.32 } }),
                         nullptr);

    project.appendChild (
        makeAutomation (4, "Insert 4 > Delay > Feedback", AutomationScope::mixerEffect, 4, 0,
                        ids::feedback,
                        { { 0.0, 0.20 }, { 48.0, 0.85, 0.40 }, { 64.0, 0.25, 0.0, "step" } }),
        nullptr);

    // The tempo itself, which is a scope of its own because it belongs to the
    // arrangement rather than to anything in it. Logarithmic, like cutoff: the
    // values below are 126, 120 and 132 bpm through that mapping.
    project.appendChild (makeAutomation (5, "Tempo", AutomationScope::project, 0, -1, ids::tempoBpm,
                                         { { 0.0, 0.4706 },
                                           { 192.0, 0.4706 },
                                           { 256.0, 0.4581, 0.0, "step" },
                                           { 320.0, 0.4825, 0.35 },
                                           { 512.0, 0.4825 } }),
                         nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);

    // Six lanes, because six things are happening at once and a lane per idea
    // is what makes that readable rather than a stack of overlapping boxes.
    while (playlist.getNumChildren() < 6)
        playlist.appendChild (
            makePlaylistTrack ("Track " + juce::String (playlist.getNumChildren() + 1)), nullptr);

    auto lane = [&playlist] (int index, const char* name)
    {
        auto track = playlist.getChild (index);
        track.setProperty (ids::name, name, nullptr);
        return track;
    };

    auto arrangement = lane (0, "Arrangement");
    arrangement.appendChild (makeClip (1, 0, 12), nullptr);
    arrangement.appendChild (makeClip (2, 12, 4), nullptr);
    arrangement.appendChild (makeClip (1, 16, 12), nullptr);
    arrangement.appendChild (makeClip (2, 28, 4), nullptr);

    auto filterLane = lane (1, "Filter");
    filterLane.appendChild (makeAutomationClip (1, 0, 12), nullptr);
    filterLane.appendChild (makeAutomationClip (1, 16, 12), nullptr);

    auto pumpLane = lane (2, "Pump");
    for (int bar = 0; bar < 32; bar += 4)
        if (bar < 12 || (bar >= 16 && bar < 28))
            pumpLane.appendChild (makeAutomationClip (3, bar, 4), nullptr);

    auto delayLane = lane (3, "Delay");
    delayLane.appendChild (makeAutomationClip (4, 12, 4), nullptr);
    delayLane.appendChild (makeAutomationClip (4, 28, 4), nullptr);

    lane (4, "Master").appendChild (makeAutomationClip (2, 28, 4), nullptr);
    lane (5, "Tempo").appendChild (makeAutomationClip (5, 0, 32), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
