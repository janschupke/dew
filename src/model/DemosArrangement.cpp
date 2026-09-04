// =============================================================================
// Song Structure.
//
// One of four translation units holding the demo library's CONTENT. The
// structure - the document File > New produces, and the table naming what
// ships - is ProjectFactory.cpp; the vocabulary these are written in is
// DemoBuilders.h; the loader that reads the committed .dew files rather than
// re-running any of this is DemoLibrary.h.
//
// Three lanes coming and going independently over four drums on one bus.
// The one demo whose subject is the playlist itself.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/ProjectEdits.h"
#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"

namespace dew
{

using namespace demo;

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
