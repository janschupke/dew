// =============================================================================
// Nightfall Waltz - chamber writing in 3/4, D harmonic minor, 48 bars.
// Notes from examples/nightfall.score.
//
// The demo that does nothing a dance mix would do. Four voices, detuned
// triangle stacks rather than saws, placed across the stereo field the way an
// ensemble actually sits - firsts to the left, cello to the right - and one
// room around all of them instead of an effect each. Nothing is compressed and
// nothing is driven.
//
// It closes on a ritardando, which is a curve on the TEMPO: the arrangement's
// own scope, and the only parameter in it. Nothing else in the library slows
// down, and a piece in three that ends at the speed it started is a piece that
// ends by stopping.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"
#include "model/demos/ScoreDemo.h"

namespace dew
{

using namespace demo;

namespace
{

/** A string voice: three triangles a few cents apart, slow on and slow off.

    Triangle rather than saw because a saw at four voices is a synth brass
    section, and the detune is in cents rather than in octaves because what
    makes a section sound like more than one player is that they are not quite
    in tune with each other.
*/
void bowed (juce::ValueTree channel, int octave, double attack, double release, double volume,
            double pan)
{
    setClassicOsc (channel, 0, "triangle", octave, 0.70, -7);
    setClassicOsc (channel, 1, "triangle", octave, 0.62, 6);
    setClassicOsc (channel, 2, "sine", octave, 0.34, 0);
    setAmp (channel, attack, 0.900, 0.68, release);
    setMix (channel, volume, pan);
}

} // namespace

juce::ValueTree ProjectFactory::createNightfallWaltz()
{
    auto project = compiledScore ("nightfall.score");

    if (! project.isValid())
        return createDefault();

    auto violin = channelNamed (project, "violin");
    auto viola = channelNamed (project, "viola");
    auto cello = channelNamed (project, "cello");
    auto clarinet = channelNamed (project, "clarinet");

    bowed (violin, 0, 0.070, 0.420, 0.56, -0.42);
    bowed (viola, 0, 0.090, 0.480, 0.48, -0.16);
    bowed (cello, -1, 0.110, 0.620, 0.62, 0.38);

    // The clarinet is not a string, so it is not built like one: a square with
    // its edge taken off by a sine underneath, and a faster attack than a bow.
    setClassicOsc (clarinet, 0, "square", 0, 0.44);
    setClassicOsc (clarinet, 1, "sine", 0, 0.40, -4);
    disableOsc (clarinet, 2);
    setAmp (clarinet, 0.030, 0.700, 0.72, 0.340);
    setMix (clarinet, 0.42, 0.18);

    // One eq per desk, and nothing else. The room is on the master, because in
    // a hall there is one room and every player is in it.
    mixerTrackWithId (project, 1)
        .appendChild (makeEffect (1, "eq",
                                  { { ids::lowGainDb, -3.5 },
                                    { ids::midGainDb, 1.0 },
                                    { ids::midFreq, 2200.0 },
                                    { ids::highGainDb, 1.5 } }),
                      nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (2, "eq",
                                  { { ids::lowGainDb, -2.0 },
                                    { ids::midGainDb, 0.5 },
                                    { ids::midFreq, 1200.0 },
                                    { ids::highGainDb, 0.5 } }),
                      nullptr);

    mixerTrackWithId (project, 3)
        .appendChild (makeEffect (3, "eq",
                                  { { ids::lowGainDb, 1.5 },
                                    { ids::midGainDb, 0.0 },
                                    { ids::midFreq, 600.0 },
                                    { ids::highGainDb, -2.0 } }),
                      nullptr);

    mixerTrackWithId (project, 4).appendChild (makeFilter (4, "lowpass", 5200.0, 0.5), nullptr);

    setMixerTrack (project, 1, 0.66, -0.10);
    setMixerTrack (project, 2, 0.58, -0.04);
    setMixerTrack (project, 3, 0.70, 0.10);
    setMixerTrack (project, 4, 0.54, 0.06);

    auto master = masterOf (project);
    master.appendChild (makeEffect (5, "reverb",
                                    { { ids::roomSize, 0.88 },
                                      { ids::damping, 0.46 },
                                      { ids::width, 1.0 },
                                      { ids::mix, 0.34 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (0.93), nullptr);

    // --- the ritardando -------------------------------------------------------
    // 96 held through the piece, easing to 74 across the last eight bars. Real
    // units, put through tempoBpm's own logarithmic mapping - the shipped
    // library wrote 0.4706 with a comment saying it meant 126, which is one
    // fact in two places and only one of them in the file.
    const auto bar = stepsPerBar (project);
    const auto tempoAt = [] (double bpm)
    { return curveValue (AutomationScope::project, ids::tempoBpm, bpm); };

    project.appendChild (makeAutomation (1, AutomationScope::project, 0, -1, ids::tempoBpm,
                                         { { 0.0, tempoAt (96.0) },
                                           { bar * 40.0, tempoAt (96.0) },
                                           { bar * 44.0, tempoAt (90.0), 0.30 },
                                           { bar * 48.0, tempoAt (74.0), 0.45 } }),
                         nullptr);

    setLanes (project, { "Score", "Tempo" });
    laneAt (project, 1).appendChild (makeAutomationClip (project, 1, 0, 48), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
