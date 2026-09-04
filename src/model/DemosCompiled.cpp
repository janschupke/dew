// =============================================================================
// The three demos that get their notes from a score.
//
// One of four translation units holding the demo library's CONTENT. The
// structure - the document File > New produces, and the table naming what
// ships - is ProjectFactory.cpp; the vocabulary these are written in is
// DemoBuilders.h; the loader that reads the committed .dew files rather than
// re-running any of this is DemoLibrary.h.
//
// These are the only demos that COMPILE text, so they are the only ones
// that need the score language on their include line - which is the whole
// reason they are one file. Opening any of them puts its .score in the
// Score tab, so Compile reproduces exactly what is already playing, and a
// test pins each against the text that made it.
// =============================================================================

#include "model/ProjectFactory.h"

#include "lang/Compile.h"

#include "model/DemoLibrary.h"
#include "model/ProjectEdits.h"
#include "model/ScoreBake.h"
#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

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

} // namespace dew
