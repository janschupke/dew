#include "model/ProjectFactory.h"

#include "lang/Compile.h"

#include "model/DemoBuilders.h"
#include "model/DemoLibrary.h"
#include "model/Ids.h"
#include "model/ProjectEdits.h"
#include "model/ProjectSchema.h"
#include "model/ScoreBake.h"

namespace dew
{

// The demo library's content, one function per demo.
//
// Split out of ProjectFactory.cpp, which is now only the two things that are
// not content: the document File > New produces, and the table naming what
// ships. These change for musical reasons and that one changes for structural
// ones, and holding them apart is what keeps a demo edit from reading like a
// change to the schema.
using namespace demo;

juce::ValueTree ProjectFactory::createDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "dew demo", nullptr);
    project.setProperty (ids::tempoBpm, 124.0, nullptr);
    project.setProperty (ids::barsInSong, 16, nullptr);

    // Sixteen bars rather than four, and four patterns rather than one, because
    // a demo of a playlist that holds a single clip is a demo of a loop. Still
    // deliberately small: this is the first thing anybody opens, and every part
    // of it should be readable at a glance in the step grid it was written in.
    auto groove = patternIn (project, 1, "Groove", 16);
    auto intro = patternIn (project, 2, "Intro", 16);
    auto brk = patternIn (project, 3, "Break", 16);
    auto fill = patternIn (project, 4, "Fill", 16);

    // Kick on every beat.
    for (int step = 0; step < 16; step += 4)
        groove.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);

    // Snare on 2 and 4.
    for (int step = 4; step < 16; step += 8)
        groove.appendChild (makeNote (2, step, 1, 60, 0.9), nullptr);

    // Offbeat bass, root and fifth.
    const int bassPitches[] = { 40, 40, 47, 40, 40, 40, 45, 43 };
    for (int i = 0; i < 8; ++i)
        groove.appendChild (makeNote (3, i * 2 + 1, 1, bassPitches[i], 0.85), nullptr);

    // A lead phrase with held notes, so release and sustain are audible.
    groove.appendChild (makeNote (4, 0, 3, 72, 0.8), nullptr);
    groove.appendChild (makeNote (4, 4, 2, 76, 0.75), nullptr);
    groove.appendChild (makeNote (4, 8, 3, 79, 0.8), nullptr);
    groove.appendChild (makeNote (4, 12, 4, 74, 0.7), nullptr);

    // Intro: the pulse and the root, and nothing else yet.
    for (int step = 0; step < 16; step += 4)
        intro.appendChild (makeNote (1, step, 1, 36, 0.8), nullptr);

    for (int step = 0; step < 16; step += 8)
        intro.appendChild (makeNote (3, step, 6, 40, 0.7), nullptr);

    // Break: the kick drops out, which is what makes it a break. The lead is
    // the same phrase an octave up, so it reads as the same music, lifted.
    for (int step = 4; step < 16; step += 8)
        brk.appendChild (makeNote (2, step, 1, 60, 0.7), nullptr);

    brk.appendChild (makeNote (4, 0, 3, 84, 0.7), nullptr);
    brk.appendChild (makeNote (4, 4, 2, 88, 0.65), nullptr);
    brk.appendChild (makeNote (4, 8, 3, 91, 0.7), nullptr);
    brk.appendChild (makeNote (4, 12, 4, 86, 0.6), nullptr);

    // Fill: a snare roll that gets louder, landing back on the groove.
    for (int step = 0; step < 12; step += 4)
        fill.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);

    for (int i = 0; i < 6; ++i)
        fill.appendChild (makeNote (2, 10 + i, 1, 60, 0.55 + i * 0.08), nullptr);

    fill.appendChild (makeNote (3, 0, 8, 40, 0.85), nullptr);

    // The arrangement: in, groove, lift, groove, and a fill into whatever comes
    // next. Five clips on one lane, which is the smallest thing that is a song
    // rather than a loop.
    auto lane = project.getChildWithName (ids::PLAYLIST).getChild (0);
    lane.setProperty (ids::name, "Arrangement", nullptr);

    lane.appendChild (makeClip (2, 0, 4), nullptr);
    lane.appendChild (makeClip (1, 4, 4), nullptr);
    lane.appendChild (makeClip (3, 8, 2), nullptr);
    lane.appendChild (makeClip (1, 10, 5), nullptr);
    lane.appendChild (makeClip (4, 15, 1), nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createMelodyDemo()
{
    auto project = createDefault();
    project.setProperty (ids::name, "Piano Roll", nullptr);
    project.setProperty (ids::tempoBpm, 96.0, nullptr);
    project.setProperty (ids::barsInSong, 32, nullptr);

    // A softer set of voices than the default: this demo is about notes, not
    // drums. The melody is channel 1 deliberately - that is the channel the
    // piano roll opens on, and it should be the line the demo is about.
    auto melodyCh = channelWithId (project, 1);
    auto chordsCh = channelWithId (project, 2);
    auto bassCh = channelWithId (project, 3);
    auto counterCh = channelWithId (project, 4);

    melodyCh.setProperty (ids::name, "Melody", nullptr);
    setClassicOsc (melodyCh, 0, "saw", 0, 0.8);
    setAmp (melodyCh, 0.005, 0.120, 0.700, 0.300);

    chordsCh.setProperty (ids::name, "Chords", nullptr);
    setClassicOsc (chordsCh, 0, "triangle", 0, 0.8);
    setAmp (chordsCh, 0.020, 0.120, 0.700, 0.450);

    bassCh.setProperty (ids::name, "Bass", nullptr);
    setAmp (bassCh, 0.005, 0.120, 0.700, 0.250);

    counterCh.setProperty (ids::name, "Counter", nullptr);
    counterCh.setProperty (ids::volume, 0.4, nullptr);
    setClassicOsc (counterCh, 0, "sine", 0, 0.8);

    // Three four-bar sections, each a pattern. The A section is the one the
    // demo has always had - it is what the piano roll opens on, and the test
    // that checks this demo for chords and varied note lengths reads it.
    struct Section
    {
        int id;
        const char* name;
        int roots[4];
        int triads[4][3];
        int melody[14][4]; // step, length, pitch, velocity per cent; pitch 0 ends it
        int counterTop;
    };

    static const Section sections[] {
        { 1,
          "Am - F - C - G",
          { 45, 41, 48, 43 },
          { { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } },
          { { 0, 3, 76, 95 },
            { 4, 2, 74, 70 },
            { 6, 2, 72, 60 },
            { 8, 6, 69, 85 },
            { 16, 3, 72, 90 },
            { 20, 2, 74, 65 },
            { 22, 2, 76, 75 },
            { 24, 6, 77, 95 },
            { 32, 4, 79, 100 },
            { 38, 2, 76, 60 },
            { 40, 6, 74, 80 },
            { 48, 3, 71, 85 },
            { 52, 3, 74, 70 },
            { 56, 8, 69, 90 } },
          88 },

        { 2,
          "Dm - Bb - F - C",
          { 50, 46, 53, 48 },
          { { 62, 65, 69 }, { 58, 62, 65 }, { 57, 60, 65 }, { 60, 64, 67 } },
          { { 0, 4, 81, 90 },
            { 6, 2, 79, 65 },
            { 8, 6, 77, 85 },
            { 16, 3, 74, 85 },
            { 20, 2, 77, 70 },
            { 22, 2, 79, 80 },
            { 24, 6, 81, 95 },
            { 32, 5, 77, 90 },
            { 38, 3, 74, 65 },
            { 42, 4, 72, 75 },
            { 48, 4, 76, 85 },
            { 54, 2, 74, 60 },
            { 56, 8, 72, 85 },
            { 0, 0, 0, 0 } },
          93 },

        { 3,
          "Bridge",
          { 43, 43, 41, 41 },
          { { 55, 59, 62 }, { 55, 62, 67 }, { 53, 57, 60 }, { 53, 60, 65 } },
          { { 8, 8, 67, 70 },
            { 24, 8, 71, 75 },
            { 40, 8, 74, 80 },
            { 56, 8, 76, 85 },
            { 0, 0, 0, 0 } },
          0 },
    };

    for (const auto& section : sections)
    {
        auto pattern = patternIn (project, section.id, section.name, 64);

        for (int bar = 0; bar < 4; ++bar)
        {
            const auto start = bar * 16;

            // Bass: root on the downbeat, held, then an octave lift.
            pattern.appendChild (makeNote (3, start, 6, section.roots[bar], 0.9), nullptr);
            pattern.appendChild (makeNote (3, start + 8, 4, section.roots[bar] + 12, 0.55),
                                 nullptr);

            // Chords: three notes at once, which is the thing a step grid cannot do.
            for (const auto pitch : section.triads[bar])
                pattern.appendChild (makeNote (2, start + 2, 12, pitch, 0.5), nullptr);
        }

        // A melody with real note lengths and a velocity shape, so the piano
        // roll's velocity lane has something in it worth looking at.
        for (const auto& note : section.melody)
        {
            if (note[2] == 0)
                break;

            pattern.appendChild (makeNote (1, note[0], note[1], note[2], note[3] / 100.0), nullptr);
        }

        // A quiet counter-line an octave up, off the beat. The bridge has none:
        // taking a voice away is as much a section as adding one.
        if (section.counterTop > 0)
            for (int step = 2; step < 64; step += 8)
                pattern.appendChild (
                    makeNote (4, step, 1, section.counterTop - (step / 16) * 2, 0.35), nullptr);
    }

    // A A' B A B' bridge A - the shape of most songs anybody has ever hummed,
    // and thirty-two bars of somewhere to go rather than four bars repeated.
    auto lane = project.getChildWithName (ids::PLAYLIST).getChild (0);
    lane.setProperty (ids::name, "Arrangement", nullptr);

    const int arrangement[][2] = {
        { 1, 0 }, { 1, 4 }, { 2, 8 }, { 1, 12 }, { 3, 16 }, { 2, 20 }, { 1, 24 }, { 3, 28 },
    };

    for (const auto& clip : arrangement)
        lane.appendChild (makeClip (clip[0], clip[1], 4), nullptr);

    return canonicalTree (project, projectSpec());
}

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

    // A chain four deep, which is the documented maximum and which nothing
    // shipped had ever reached - the longest chain in the library was two.
    // Order is the point: the filter shapes the saw, the drive works on what
    // is left, the chorus widens THAT, and the delay repeats the finished
    // sound. Put the drive last and it distorts the delay's tail instead.
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

juce::ValueTree ProjectFactory::createScoreDemo()
{
    // Compiled, not loaded. The demo IS the score, so opening it puts real text
    // in the Score tab and pressing Compile reproduces exactly what is playing -
    // and `dew_render --write-demos` regenerates the .dew from the .score, which
    // is what stops the two drifting.
    const auto source = DemoLibrary::jsonFor ("amber.score");

    const auto result = lang::compile (source.toStdString(), "amber.score");

    if (! result.ok())
        return createDefault();

    BakeReport report;
    auto project = ScoreBake::toNewProject (*result.score, report);

    // A bake starts from createDefault(), and this score adopts only two of its
    // four channels by name - so Kick and Snare arrived with nothing to play,
    // and the empty Pattern 1 was what the piano roll opened on. The routing
    // needs saying too: addChannel falls back to insert 1 when no track's id
    // matches, so pad and answer both landed on the same fader.
    pruneUnplayedChannels (project);
    rebuildInserts (project);

    ProjectEdits::setScoreSource (project, source, "amber.score", nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createWavetableDemo()
{
    // The music comes from examples/drift.score and the SOUND is built here.
    // That is the language's own ownership line - it owns notes, patterns and
    // clips; the instrument, the effects and the mixer are the user's - and
    // following it means this demo can show off the wavetable oscillator
    // without the score language having to grow a word for any of it.
    const auto source = DemoLibrary::jsonFor ("drift.score");
    const auto result = lang::compile (source.toStdString(), "drift.score");

    if (! result.ok())
        return createDefault();

    BakeReport report;
    auto project = ScoreBake::toNewProject (*result.score, report);

    // A bake starts from createDefault(), so Kick, Snare, Bass and Lead are
    // sitting there with nothing to play, and Pattern 1 is empty beside them.
    pruneUnplayedChannels (project);
    rebuildInserts (project);

    auto pad = channelNamed (project, "pad");
    auto swell = channelNamed (project, "swell");
    auto sub = channelNamed (project, "sub");
    auto bell = channelNamed (project, "bell");
    auto answer = channelNamed (project, "answer");

    // All five factory tables, one per voice, so the demo is a tour of the bank
    // rather than one table five times.
    //
    // The two position sources are the thing to listen for. A held voice takes
    // "lfo", which runs free at its own rate and keeps a four-bar chord moving;
    // a struck voice takes "envelope", which sweeps the morph across the length
    // of the note it is in, so the position is part of the attack rather than a
    // separate motion. A pad on "envelope" would arrive at its timbre and stop.
    pad.setProperty (ids::volume, 0.30, nullptr);
    setWavetableOsc (pad, 0, "formant", 0.15, 0.55, "lfo", 0.08, 7, 14.0);

    // A second slot an octave down, on a different table: unison spread is
    // what makes one oscillator wide, and two slots are what make it deep.
    setWavetableOsc (pad, 1, "harmonics", 0.35, 0.25, "lfo", 0.05, 5, 22.0, -1, 0.45);
    setAmp (pad, 1.200, 0.600, 0.850, 2.400);

    swell.setProperty (ids::volume, 0.22, nullptr);
    setWavetableOsc (swell, 0, "harmonics", 0.05, 0.90, "envelope", 1.0, 5, 16.0);
    setAmp (swell, 1.800, 1.200, 0.700, 2.000);

    // No unison on the bass. Seven detuned copies of a low D beat against each
    // other slowly enough to hear as wobble rather than as width.
    sub.setProperty (ids::volume, 0.45, nullptr);
    setWavetableOsc (sub, 0, "fold", 0.10, 0.35, "envelope", 1.0, 1, 0.0, 0, 0.9);
    setAmp (sub, 0.020, 0.500, 0.600, 0.500);

    bell.setProperty (ids::volume, 0.30, nullptr);
    setWavetableOsc (bell, 0, "pulse", 0.80, -0.55, "envelope", 1.0, 3, 8.0, 0, 0.75);
    setAmp (bell, 0.002, 0.350, 0.120, 0.900);

    answer.setProperty (ids::volume, 0.22, nullptr);
    setWavetableOsc (answer, 0, "basic", 0.25, 0.40, "lfo", 0.30, 5, 11.0, 0, 0.7);
    setAmp (answer, 0.400, 0.400, 0.600, 1.100);

    pad.appendChild (makeEffect (1, "filter", { { ids::cutoff, 2200.0 }, { ids::resonance, 0.9 } }),
                     nullptr);
    pad.appendChild (
        makeEffect (2, "chorus", { { ids::rate, 0.25 }, { ids::depth, 0.60 }, { ids::mix, 0.50 } }),
        nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);

    // Five sustaining voices into a shared reverb sum fast, so the inserts sit
    // back from the 0.8 a fresh track carries. Backed off further than this,
    // the demo was quieter than every other one in the library.
    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            track.setProperty (ids::gain, 0.80, nullptr);

    if (auto bellTrack = ProjectEdits::findMixerTrack (project, (int) bell[ids::mixerTrackId]);
        bellTrack.isValid())
        bellTrack.appendChild (
            makeEffect (3, "delay",
                        { { ids::delayMs, 444.0 }, { ids::feedback, 0.42 }, { ids::mix, 0.32 } }),
            nullptr);

    // On the MASTER, which every demo before this one left empty even though
    // the master has carried a chain as long as an insert has.
    if (auto master = mixer.getChildWithName (ids::MASTER); master.isValid())
    {
        master.setProperty (ids::gain, 0.90, nullptr);
        master.appendChild (makeEffect (4, "reverb",
                                        { { ids::roomSize, 0.90 },
                                          { ids::damping, 0.20 },
                                          { ids::width, 1.00 },
                                          { ids::mix, 0.35 } }),
                            nullptr);
    }

    // The morph, drawn. `channelOsc` is the scope nothing shipped had ever
    // used, and it is the one that makes a wavetable a wavetable rather than a
    // waveform somebody picked once.
    //
    // Sixteen steps to a bar, so the arrangement's 32 bars are 512 steps.
    project.appendChild (
        makeAutomation (
            1, "Pad > Osc 1 > Position", AutomationScope::channelOsc, (int) pad[ids::id], 0,
            ids::wavePosition,
            { { 0.0, 0.10 }, { 160.0, 0.45, 0.35 }, { 320.0, 0.85 }, { 512.0, 0.20, -0.30 } }),
        nullptr);

    // Held flat and then jumped, rather than swept: a "step" segment is a
    // change of timbre you hear arrive, and the bell is struck often enough
    // that a slow sweep across it would read as drift rather than as a change.
    project.appendChild (
        makeAutomation (
            2, "Bell > Osc 1 > Position", AutomationScope::channelOsc, (int) bell[ids::id], 0,
            ids::wavePosition,
            { { 0.0, 0.90, 0.0, "step" }, { 128.0, 0.35, 0.0, "step" }, { 256.0, 0.70 } }),
        nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);

    playlist.getChild (0).setProperty (ids::name, "Pad morph", nullptr);
    playlist.getChild (0).appendChild (makeAutomationClip (1, 0, 32), nullptr);

    playlist.getChild (1).setProperty (ids::name, "Bell morph", nullptr);
    playlist.getChild (1).appendChild (makeAutomationClip (2, 8, 16), nullptr);

    // The score travels IN the project, so opening this demo puts the text in
    // the Score tab and Compile reproduces exactly what is already playing.
    ProjectEdits::setScoreSource (project, source, "drift.score", nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createLayersDemo()
{
    // Every demo before this one used oscillator slot ONE and left the other
    // two switched off, which made three quarters of the instrument panel look
    // like decoration. This is the demo for the panel: four channels, three
    // slots each, and no channel whose sound is a single waveform.
    const auto source = DemoLibrary::jsonFor ("neon.score");
    const auto result = lang::compile (source.toStdString(), "neon.score");

    if (! result.ok())
        return createDefault();

    BakeReport report;
    auto project = ScoreBake::toNewProject (*result.score, report);

    pruneUnplayedChannels (project);
    rebuildInserts (project);

    auto lead = channelNamed (project, "lead");
    auto pad = channelNamed (project, "pad");
    auto bass = channelNamed (project, "bass");
    auto arp = channelNamed (project, "arp");

    // Two saws a few cents apart and a square an octave up. The detune is what
    // makes it wide - two oscillators at exactly the same pitch are one louder
    // oscillator - and nine cents is about as far as it goes before the beating
    // is heard as two notes rather than as one thick one.
    lead.setProperty (ids::volume, 0.28, nullptr);
    setClassicOsc (lead, 0, "saw", 0, 0.75, 0);
    setClassicOsc (lead, 1, "saw", 0, 0.65, 9);
    setClassicOsc (lead, 2, "square", 1, 0.28, -4);
    setAmp (lead, 0.012, 0.220, 0.650, 0.320);

    // A stack does not have to be all one kind. Slot three here is a WAVETABLE
    // beside two classic oscillators, which the schema has always allowed - the
    // mode is a property of the slot, not of the channel - and which nothing
    // shipped had ever put in front of anyone.
    pad.setProperty (ids::volume, 0.22, nullptr);
    setClassicOsc (pad, 0, "triangle", 0, 0.60, 0);
    setClassicOsc (pad, 1, "saw", 0, 0.45, -7);
    setWavetableOsc (pad, 2, "harmonics", 0.30, 0.20, "lfo", 0.12, 5, 12.0, -1, 0.35);
    setAmp (pad, 0.450, 0.700, 0.800, 1.100);

    // The sub is its own slot an octave down, not the bass turned up: a sine
    // under a saw is a low end you can hear on a speaker that cannot reproduce
    // the saw's fundamental at all.
    bass.setProperty (ids::volume, 0.35, nullptr);
    setClassicOsc (bass, 0, "sine", -1, 0.90, 0);
    setClassicOsc (bass, 1, "saw", 0, 0.55, 0);
    setClassicOsc (bass, 2, "square", 0, 0.25, 5);
    setAmp (bass, 0.004, 0.260, 0.550, 0.140);

    arp.setProperty (ids::volume, 0.20, nullptr);
    setClassicOsc (arp, 0, "square", 0, 0.60, 0);
    setClassicOsc (arp, 1, "square", 1, 0.30, -12);
    setClassicOsc (arp, 2, "triangle", -1, 0.40, 0);
    setAmp (arp, 0.002, 0.140, 0.000, 0.090);

    lead.appendChild (
        makeEffect (1, "filter", { { ids::cutoff, 5200.0 }, { ids::resonance, 1.1 } }), nullptr);
    pad.appendChild (
        makeEffect (2, "chorus", { { ids::rate, 0.45 }, { ids::depth, 0.35 }, { ids::mix, 0.45 } }),
        nullptr);

    auto mixer = project.getChildWithName (ids::MIXER);

    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            track.setProperty (ids::gain, 0.90, nullptr);

    if (auto arpTrack = ProjectEdits::findMixerTrack (project, (int) arp[ids::mixerTrackId]);
        arpTrack.isValid())
        arpTrack.appendChild (
            makeEffect (3, "delay",
                        { { ids::delayMs, 278.0 }, { ids::feedback, 0.38 }, { ids::mix, 0.28 } }),
            nullptr);

    // The one curve here, and it is pointed at the wavetable slot inside the
    // classic stack: the pad's timbre opens over the arrangement while the two
    // oscillators beside it hold still.
    project.appendChild (makeAutomation (1, "Pad > Osc 3 > Position", AutomationScope::channelOsc,
                                         (int) pad[ids::id], 2, ids::wavePosition,
                                         { { 0.0, 0.12 }, { 256.0, 0.55, 0.40 }, { 512.0, 0.90 } }),
                         nullptr);

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    playlist.getChild (0).setProperty (ids::name, "Pad morph", nullptr);
    playlist.getChild (0).appendChild (makeAutomationClip (1, 0, 32), nullptr);

    ProjectEdits::setScoreSource (project, source, "neon.score", nullptr);

    return canonicalTree (project, projectSpec());
}

juce::ValueTree ProjectFactory::createArrangementDemo()
{
    // The demo for the PLAYLIST. Every other demo in the library puts its
    // pattern clips on one lane, which is a timeline rather than an
    // arrangement: what a lane is for is deciding that the drums come in at
    // bar 4, the bass at bar 8 and the lead at bar 16, independently.
    //
    // And the mixer's other trick, which nothing shipped had shown: four drum
    // channels share one insert, so the reverb, the fader and the meter are the
    // kit's rather than the kick's.
    auto project = scaffold (8);
    project.setProperty (ids::name, "Song Structure", nullptr);
    project.setProperty (ids::tempoBpm, 128.0, nullptr);
    project.setProperty (ids::barsInSong, 32, nullptr);

    struct Voice
    {
        int id;
        const char* name;
        int bus;
        int basePitch;
        const char* wave;
        double volume;
        double attack, decay, sustain, release;
    };

    static const Voice voices[] {
        { 1, "Kick", 1, 36, "sine", 0.95, 0.001, 0.150, 0.00, 0.060 },
        { 2, "Clap", 1, 62, "square", 0.42, 0.001, 0.090, 0.00, 0.070 },
        { 3, "Hat", 1, 84, "square", 0.16, 0.001, 0.030, 0.00, 0.025 },
        { 4, "Perc", 1, 70, "triangle", 0.24, 0.001, 0.070, 0.00, 0.050 },
        { 5, "Bass", 2, 33, "saw", 0.55, 0.003, 0.200, 0.55, 0.100 },
        { 6, "Pad", 3, 57, "saw", 0.18, 0.400, 0.600, 0.80, 0.900 },
        { 7, "Lead", 4, 76, "square", 0.22, 0.008, 0.200, 0.45, 0.250 },
        { 8, "Stab", 5, 69, "saw", 0.20, 0.004, 0.140, 0.20, 0.180 },
    };

    for (const auto& voice : voices)
    {
        auto channel = channelWithId (project, voice.id);
        channel.setProperty (ids::name, voice.name, nullptr);
        channel.setProperty (ids::basePitch, voice.basePitch, nullptr);
        channel.setProperty (ids::volume, voice.volume, nullptr);
        setClassicOsc (channel, 0, voice.wave, 0, 0.8);
        setAmp (channel, voice.attack, voice.decay, voice.sustain, voice.release);
        routeTo (channel, voice.bus);
    }

    // Five inserts for eight channels. The mixer strip for "Drums" lists all
    // four channels feeding it, and clicking one goes there.
    setInserts (project, { "Drums", "Bass", "Pad", "Lead", "Stab" });

    auto mixer = project.getChildWithName (ids::MIXER);

    for (auto track : mixer)
        if (track.hasType (ids::MIXER_TRACK))
            track.setProperty (ids::gain, 0.75, nullptr);

    // On the bus, so it treats the kit as one sound rather than four. Glue on a
    // drum bus is the reason a bus exists.
    if (auto drums = ProjectEdits::findMixerTrack (project, 1); drums.isValid())
        drums.appendChild (
            makeEffect (1, "drive",
                        { { ids::drive, 2.2 }, { ids::outputGain, 0.85 }, { ids::mix, 0.35 } }),
            nullptr);

    if (auto pad = ProjectEdits::findMixerTrack (project, 3); pad.isValid())
        pad.appendChild (
            makeEffect (2, "reverb",
                        { { ids::roomSize, 0.75 }, { ids::damping, 0.35 }, { ids::mix, 0.30 } }),
            nullptr);

    if (auto lead = ProjectEdits::findMixerTrack (project, 4); lead.isValid())
        lead.appendChild (
            makeEffect (3, "delay",
                        { { ids::delayMs, 234.0 }, { ids::feedback, 0.40 }, { ids::mix, 0.26 } }),
            nullptr);

    // --- the patterns ---------------------------------------------------------
    // Each holds only the channels its LANE is about, which is what makes three
    // lanes independent: a pattern carrying every channel would put the whole
    // song in one clip and there would be nothing to arrange.
    auto beatA = patternIn (project, 1, "Beat A", 64);
    auto beatB = patternIn (project, 2, "Beat B", 64);
    auto bassA = patternIn (project, 3, "Bassline", 64);
    auto bassB = patternIn (project, 4, "Bass Drop", 64);
    auto chords = patternIn (project, 5, "Chords", 64);
    auto lead = patternIn (project, 6, "Lead", 64);

    const int roots[4] = { 33, 33, 29, 31 };
    const int triads[4][3] = { { 57, 60, 64 }, { 57, 60, 64 }, { 53, 56, 60 }, { 55, 58, 62 } };

    for (int bar = 0; bar < 4; ++bar)
    {
        const auto at = bar * 16;

        for (int step = at; step < at + 16; step += 4)
        {
            beatA.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);
            beatB.appendChild (makeNote (1, step, 1, 36, 1.0), nullptr);
        }

        for (int step = at + 4; step < at + 16; step += 8)
        {
            beatA.appendChild (makeNote (2, step, 1, 62, 0.85), nullptr);
            beatB.appendChild (makeNote (2, step, 1, 62, 0.85), nullptr);
        }

        for (int step = at + 2; step < at + 16; step += 4)
            beatA.appendChild (makeNote (3, step, 1, 84, 0.5), nullptr);

        // Beat B is the same beat with the hats doubled and a perc line on top,
        // which is how a second half differs from a first without changing key.
        for (int step = at; step < at + 16; step += 2)
            beatB.appendChild (makeNote (3, step, 1, 84, step % 4 == 2 ? 0.55 : 0.3), nullptr);

        for (const auto offset : { 6, 11, 14 })
            beatB.appendChild (makeNote (4, at + offset, 1, 70 + offset % 5, 0.6), nullptr);

        for (int i = 0; i < 8; ++i)
            bassA.appendChild (makeNote (5, at + i * 2, 1, roots[bar], i % 4 == 0 ? 0.9 : 0.7),
                               nullptr);

        bassB.appendChild (makeNote (5, at, 12, roots[bar] - 12, 0.95), nullptr);
        bassB.appendChild (makeNote (5, at + 12, 3, roots[bar] - 5, 0.7), nullptr);

        for (const auto pitch : triads[bar])
            chords.appendChild (makeNote (6, at, 15, pitch, 0.55), nullptr);

        const int phrase[4] = { 76, 79, 83, 79 };
        lead.appendChild (makeNote (7, at, 6, phrase[bar], 0.8), nullptr);
        lead.appendChild (makeNote (7, at + 8, 4, phrase[bar] + 5, 0.7), nullptr);

        for (const auto pitch : triads[bar])
            lead.appendChild (makeNote (8, at + 6, 2, pitch + 12, 0.5), nullptr);
    }

    // --- the arrangement ------------------------------------------------------
    // Three lanes, each entering and leaving on its own bar. Read down a column
    // and you have what is playing; read across a row and you have one voice's
    // whole part.
    setLanes (project, { "Drums", "Bass", "Keys" });

    auto playlist = project.getChildWithName (ids::PLAYLIST);
    auto drumLane = playlist.getChild (0);
    auto bassLane = playlist.getChild (1);
    auto keysLane = playlist.getChild (2);

    // The drums arrive at bar 4, which is what makes the first four bars an
    // intro rather than a mistake.
    drumLane.appendChild (makeClip (1, 4, 8), nullptr);
    drumLane.appendChild (makeClip (2, 12, 8), nullptr);
    drumLane.appendChild (makeClip (1, 20, 4), nullptr);
    drumLane.appendChild (makeClip (2, 24, 8), nullptr);

    bassLane.appendChild (makeClip (3, 8, 12), nullptr);
    bassLane.appendChild (makeClip (4, 20, 4), nullptr);
    bassLane.appendChild (makeClip (3, 24, 8), nullptr);

    keysLane.appendChild (makeClip (5, 0, 16), nullptr);
    keysLane.appendChild (makeClip (6, 16, 12), nullptr);
    keysLane.appendChild (makeClip (5, 28, 4), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
