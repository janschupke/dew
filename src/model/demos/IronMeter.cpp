// =============================================================================
// Iron Meter - math rock in 7/8, 56 bars. Notes from examples/ironmeter.score,
// kit written here.
//
// The hybrid. A score owns harmony, voice leading and melody; a drum part is
// none of those - it is placement, and the thing that makes THIS one worth
// hearing is that the kit groups the bar 3+2+2 while the guitar groups it
// 2+2+3. Neither part is wrong and the bar is the same length; that argument is
// what the piece is about, and it cannot be written in a language whose unit is
// a chord.
//
// So the drums are added into the patterns the bake produced, sized from each
// pattern's own length. Nothing else in the library does this, and it is the
// answer to "how does a score-driven demo get a real kit".
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/EntityColour.h"
#include "model/Ids.h"
#include "model/Meter.h"
#include "model/ProjectSchema.h"
#include "model/demos/ScoreDemo.h"

namespace dew
{

using namespace demo;

namespace
{

constexpr int kKickId = 5;
constexpr int kSnareId = 6;
constexpr int kHatId = 7;

/** Appends a drum channel the bake knows nothing about. */
juce::ValueTree addDrum (juce::ValueTree project, int id, const char* name, int colourIndex,
                         const char* wave, int octave, double decay, double volume, double pan)
{
    auto channel = makeChannel (id, name, entityColour::defaultHex (colourIndex), 36, wave, octave,
                                0.001, decay, 0.0, decay * 0.6, volume);
    channel.setProperty (ids::pan, pan, nullptr);
    project.appendChild (channel, nullptr);
    return channel;
}

/** One bar of the kit, grouped 3+2+2 against a guitar grouped 2+2+3.

    `beat` is the steps in a beat, so this reads the same whatever grid the
    score's durations happened to require.
*/
void barOfKit (juce::ValueTree pattern, int at, int beat, bool busy)
{
    // 7/8 counted 3+2+2: the kick marks the long group and the two short ones.
    pattern.appendChild (makeNote (kKickId, at, beat, 33, 0.95), nullptr);
    pattern.appendChild (makeNote (kKickId, at + beat * 3, beat, 33, 0.82), nullptr);

    if (busy)
        pattern.appendChild (makeNote (kKickId, at + beat * 5, beat, 33, 0.7), nullptr);

    pattern.appendChild (makeNote (kSnareId, at + beat * 2, beat, 62, 0.85), nullptr);
    pattern.appendChild (makeNote (kSnareId, at + beat * 5, beat, 62, 0.78), nullptr);

    for (int eighth = 0; eighth < 7; ++eighth)
    {
        // The accents fall on the guitar's grouping rather than the kick's, so
        // the two are audibly arguing rather than merely coexisting.
        const auto accent = eighth == 0 || eighth == 2 || eighth == 4;
        pattern.appendChild (
            makeNote (kHatId, at + beat * eighth, beat / 2 + 1, 90, accent ? 0.5 : 0.3), nullptr);
    }
}

} // namespace

juce::ValueTree ProjectFactory::createIronMeter()
{
    auto project = compiledScore ("ironmeter.score");

    if (! project.isValid())
        return createDefault();

    auto guitar = channelNamed (project, "guitar");
    auto bass = channelNamed (project, "bass");
    auto keys = channelNamed (project, "keys");
    auto lead = channelNamed (project, "lead");

    setClassicOsc (guitar, 0, "saw", 0, 0.72, -9);
    setClassicOsc (guitar, 1, "saw", 0, 0.68, 10);
    setClassicOsc (guitar, 2, "square", -1, 0.30);
    setAmp (guitar, 0.002, 0.240, 0.62, 0.120);
    setMix (guitar, 0.60, -0.22);

    setClassicOsc (bass, 0, "saw", -1, 0.80);
    setClassicOsc (bass, 1, "sine", -2, 0.55);
    setAmp (bass, 0.003, 0.200, 0.70, 0.090);
    setMix (bass, 0.78, 0.0);

    setClassicOsc (keys, 0, "square", 0, 0.42, -4);
    setClassicOsc (keys, 1, "triangle", 1, 0.30, 5);
    setAmp (keys, 0.004, 0.300, 0.35, 0.200);
    setMix (keys, 0.38, 0.26);

    setWavetableOsc (lead, 0, "fold", 0.36, 0.55, "envelope", 0.5, 3, 12.0, 0, 0.66);
    setAmp (lead, 0.004, 0.280, 0.48, 0.180);
    setMix (lead, 0.50, 0.10);

    auto kick = addDrum (project, kKickId, "Kick", 4, "sine", -1, 0.150, 0.92, 0.0);
    auto snare = addDrum (project, kSnareId, "Snare", 5, "square", 0, 0.110, 0.58, -0.06);
    auto hat = addDrum (project, kHatId, "Hat", 6, "square", 2, 0.032, 0.30, 0.20);

    setClassicOsc (snare, 1, "saw", 1, 0.38, 33);
    setClassicOsc (hat, 1, "square", 2, 0.26, 47);

    // The score's own channels keep inserts 1..4; the kit gets a fifth of its
    // own, so three channels sit on one fader.
    setInserts (project, { "Guitar", "Bass", "Keys", "Lead", "Drums" });

    routeTo (kick, 5);
    routeTo (snare, 5);
    routeTo (hat, 5);

    // Six deep, and one of them switched OFF rather than removed. A bypassed
    // slot is a thing a chain can hold and nothing shipped had ever held: it is
    // how a mix keeps an idea it is not currently using.
    auto guitarInsert = mixerTrackWithId (project, 1);
    guitarInsert.appendChild (makeFilter (1, "highpass", 110.0, 0.7), nullptr);
    guitarInsert.appendChild (makeDistortion (2, "hardClip", 7.5, 0.55, 0.44), nullptr);
    guitarInsert.appendChild (makeFilter (3, "bandpass", 1500.0, 0.9, 0.35), nullptr);
    guitarInsert.appendChild (makeEffect (4, "phaser",
                                          { { ids::rate, 0.35 },
                                            { ids::depth, 0.5 },
                                            { ids::centreFreq, 1100.0 },
                                            { ids::feedback, 0.4 },
                                            { ids::mix, 0.4 } }),
                              nullptr);

    auto bypassed = makeEffect (5, "chorus",
                                { { ids::rate, 0.8 }, { ids::depth, 0.4 }, { ids::mix, 0.5 } });
    bypassed.setProperty (ids::enabled, false, nullptr);
    guitarInsert.appendChild (bypassed, nullptr);

    guitarInsert.appendChild (makeEffect (6, "eq",
                                          { { ids::lowGainDb, -2.0 },
                                            { ids::midGainDb, 2.5 },
                                            { ids::midFreq, 1800.0 },
                                            { ids::highGainDb, 1.0 } }),
                              nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeDistortion (7, "fold", 3.2, 0.40, 0.62, 0.35), nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (
            makeEffect (8, "delay",
                        { { ids::delayMs, 204.0 }, { ids::feedback, 0.36 }, { ids::mix, 0.26 } }),
            nullptr);

    auto drumInsert = mixerTrackWithId (project, 5);
    drumInsert.appendChild (makeEffect (9, "compressor",
                                        { { ids::threshold, -18.0 },
                                          { ids::ratio, 5.0 },
                                          { ids::attackMs, 3.0 },
                                          { ids::releaseMs, 80.0 },
                                          { ids::makeup, 3.5 } }),
                            nullptr);

    setMixerTrack (project, 1, 0.62, -0.08);
    setMixerTrack (project, 2, 0.72, 0.0);
    setMixerTrack (project, 3, 0.42, 0.12);
    setMixerTrack (project, 4, 0.50, 0.06);
    setMixerTrack (project, 5, 0.76, 0.0);

    auto master = masterOf (project);
    master.appendChild (makeEffect (10, "limiter", { { ids::ceiling, -1.0 } }), nullptr);
    master.setProperty (ids::gain, stored (1.30), nullptr);

    // --- the kit, into the patterns the bake made -----------------------------
    const auto meter = Meter::of (project);
    const auto bar = meter.stepsPerBar();
    auto patterns = 0;

    for (auto pattern : project)
    {
        if (! pattern.hasType (ids::PATTERN))
            continue;

        const auto bars = (int) pattern[ids::lengthSteps] / bar;
        ++patterns;

        for (int i = 0; i < bars; ++i)
            barOfKit (pattern, i * bar, meter.stepsPerBeat, patterns > 2 && (i % 4) == 3);
    }

    // --- the accelerando ------------------------------------------------------
    // Into the last section rather than out of it. 147 to 158 over eight bars,
    // and back for the coda: a math-rock band pushing the final riff is a real
    // thing, and it is the only tempo movement in the library that speeds up.
    const auto tempoAt = [] (double bpm)
    { return curveValue (AutomationScope::project, ids::tempoBpm, bpm); };

    project.appendChild (makeAutomation (1, AutomationScope::project, 0, -1, ids::tempoBpm,
                                         { { 0.0, tempoAt (294.0) },
                                           { bar * 32.0, tempoAt (294.0) },
                                           { bar * 40.0, tempoAt (316.0), -0.35 },
                                           { bar * 48.0, tempoAt (316.0) },
                                           { bar * 56.0, tempoAt (280.0), 0.25 } }),
                         nullptr);

    setLanes (project, { "Score", "Tempo" });
    laneAt (project, 1).appendChild (makeAutomationClip (project, 1, 0, 56), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
