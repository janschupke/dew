// =============================================================================
// Late Rhodes - neo-soul ballad, 78 bpm, 4/4 swung, Eb major, 40 bars.
// Notes from examples/rhodes.score.
//
// This is the demo that owns the FM MATRIX, and it owns it by being an
// instrument that is genuinely made of one. An electric piano is a sine folded
// into a sine: one operator an octave above the carrier, at an amount low
// enough to stay a bell and high enough to stop being a flute. Every shipped
// project before this had every fmTo at zero and every fmOut at one - three
// oscillators summed in parallel, which is what the synth was before the matrix
// existed.
//
// One honest limit, written down rather than worked around: dew's amplitude
// envelope belongs to the CHANNEL, not to an operator. A real DX tine is a
// high-ratio operator with an envelope of its own that is gone in eighty
// milliseconds. Here the tine can only be a third sine mixed low, and it is.
// =============================================================================

#include "model/ProjectFactory.h"

#include "model/DemoBuilders.h"
#include "model/Ids.h"
#include "model/ProjectSchema.h"
#include "model/demos/ScoreDemo.h"

namespace dew
{

using namespace demo;

juce::ValueTree ProjectFactory::createLateRhodes()
{
    auto project = compiledScore ("rhodes.score");

    if (! project.isValid())
        return createDefault();

    auto rhodes = channelNamed (project, "rhodes");
    auto bass = channelNamed (project, "bass");
    auto pad = channelNamed (project, "pad");
    auto horn = channelNamed (project, "horn");

    // Slot 1 is the carrier and the only thing heard in full. Slot 2 is the
    // modulator: an octave up, folded into slot 1 at 0.30, and OUT at zero, so
    // nothing hears it directly. Slot 3 is the tine, three octaves up, heard at
    // a tenth - see the note above about why it cannot be an operator.
    setClassicOsc (rhodes, 0, "sine", 0, 0.85);
    setClassicOsc (rhodes, 1, "sine", 1, 0.70);
    setClassicOsc (rhodes, 2, "sine", 3, 0.55, 4);
    setFm (rhodes, 0, 0.0, 0.0, 0.0, 1.0);
    setFm (rhodes, 1, 0.30, 0.0, 0.0, 0.0);
    setFm (rhodes, 2, 0.08, 0.0, 0.0, 0.10);
    setAmp (rhodes, 0.002, 1.300, 0.22, 0.520);
    setMix (rhodes, 0.66, -0.06);

    setClassicOsc (bass, 0, "sine", -1, 0.90);
    setClassicOsc (bass, 1, "triangle", 0, 0.30, -5);
    setAmp (bass, 0.006, 0.420, 0.55, 0.180);
    setMix (bass, 0.80, 0.0);

    setWavetableOsc (pad, 0, "harmonics", 0.34, 0.30, "lfo", 0.11, 3, 9.0, 0, 0.45);
    setAmp (pad, 0.600, 1.600, 0.62, 1.400);
    setMix (pad, 0.30, 0.20);

    // The horn is FM too, but the other way round: the modulator is a fifth
    // above rather than an octave, which is what makes it reedy instead of
    // bell-like. Same matrix, different interval, audibly different instrument.
    setClassicOsc (horn, 0, "triangle", 0, 0.75);
    setClassicOsc (horn, 1, "sine", 1, 0.50, 2);
    setFm (horn, 1, 0.18, 0.0, 0.0, 0.0);
    disableOsc (horn, 2);
    setAmp (horn, 0.040, 0.500, 0.62, 0.300);
    setMix (horn, 0.46, 0.12);

    mixerTrackWithId (project, 1)
        .appendChild (
            makeEffect (1, "chorus",
                        { { ids::rate, 0.62 }, { ids::depth, 0.35 }, { ids::mix, 0.42 } }),
            nullptr);
    mixerTrackWithId (project, 1)
        .appendChild (makeEffect (2, "eq",
                                  { { ids::lowGainDb, -2.0 },
                                    { ids::midGainDb, 1.5 },
                                    { ids::midFreq, 1600.0 },
                                    { ids::highGainDb, 2.5 } }),
                      nullptr);

    mixerTrackWithId (project, 2)
        .appendChild (makeEffect (3, "eq",
                                  { { ids::lowGainDb, 2.5 },
                                    { ids::midGainDb, -1.0 },
                                    { ids::midFreq, 450.0 },
                                    { ids::highGainDb, -3.0 } }),
                      nullptr);

    mixerTrackWithId (project, 3)
        .appendChild (makeEffect (4, "reverb",
                                  { { ids::roomSize, 0.78 },
                                    { ids::damping, 0.5 },
                                    { ids::width, 1.0 },
                                    { ids::mix, 0.5 } }),
                      nullptr);

    mixerTrackWithId (project, 4)
        .appendChild (
            makeEffect (5, "delay",
                        { { ids::delayMs, 462.0 }, { ids::feedback, 0.30 }, { ids::mix, 0.24 } }),
            nullptr);

    setMixerTrack (project, 1, 0.76, 0.0);
    setMixerTrack (project, 2, 0.70, 0.0);
    setMixerTrack (project, 3, 0.40, 0.14);
    setMixerTrack (project, 4, 0.52, -0.10);

    auto master = masterOf (project);
    master.appendChild (makeEffect (6, "compressor",
                                    { { ids::threshold, -12.0 },
                                      { ids::ratio, 2.2 },
                                      { ids::attackMs, 18.0 },
                                      { ids::releaseMs, 220.0 },
                                      { ids::makeup, 1.5 } }),
                        nullptr);
    master.appendChild (makeEffect (7, "eq",
                                    { { ids::lowGainDb, 1.0 },
                                      { ids::midGainDb, -0.5 },
                                      { ids::midFreq, 800.0 },
                                      { ids::highGainDb, 1.5 } }),
                        nullptr);
    master.setProperty (ids::gain, stored (1.05), nullptr);

    // The horn enters at the second chorus and leaves before the tag, which a
    // curve on its own volume says more honestly than muting a lane would.
    const auto bar = stepsPerBar (project);
    const auto hornId = (int) horn[ids::id];
    const auto quiet = curveValue (AutomationScope::channel, ids::volume, 0.06);
    const auto full = curveValue (AutomationScope::channel, ids::volume, 0.46);

    project.appendChild (makeAutomation (1, AutomationScope::channel, hornId, -1, ids::volume,
                                         { { 0.0, quiet },
                                           { bar * 2.0, full, -0.30 },
                                           { bar * 22.0, full },
                                           { bar * 26.0, quiet, 0.30 } }),
                         nullptr);

    setLanes (project, { "Score", "Horn" });
    laneAt (project, 1).appendChild (makeAutomationClip (project, 1, 8, 24), nullptr);

    return canonicalTree (project, projectSpec());
}

} // namespace dew
